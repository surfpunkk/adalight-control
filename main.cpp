#include <string>
#include <vector>
#include <cmath>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <map>

class Config {
public:
	const std::string config = "/etc/skydimo-control/skydimo-control.conf"; // config path
	std::string port = "/dev/ttyUSB0"; // or your any other device which you can see with command 'lsusb'
	int num_leds = 255; // default value ( i guess )
	int speed_port = 115200; // also default value
	float speed = 0.15f; // default speed
	std::string mode = "red-blue"; // default mode
	float r = 0, g = 0, b = 0;
	void load_config() { // reading existing config
		std::ifstream file(config);
		std::string line;
		while (std::getline(file, line)) {
			std::stringstream ss(line);
			std::string word;
			if (std::getline(ss, word, '=')) {
				std::string value;
				if (word == "port") ss >> port;
				else if (word == "num_leds") ss >> num_leds;
				else if (word == "speed_port") ss >> speed_port;
				else if (word == "mode") ss >> mode;
				else if (word == "speed") ss >> speed;
			}
		}
	}
	Config () { load_config(); }
};

class Effect {
	friend class Run;
	Config &cfg;
	float t = 0.0f;
	std::map <std::string, std::vector<float>, std::less<>> colors = { // colour base
		{"red", {255, 0, 0}},
		{"green", {0, 255, 0}},
		{"blue", {0, 0, 255}},
		{"cyan", {0, 255, 255}},
		{"yellow", {255, 255, 0}},
		{"magenta", {255, 0, 255}},
		{"orange", {255, 165, 0}},
		{"turquoise", {0, 255, 215}},
		{"white", {255, 255, 255}},
	};

	std::pair<const std::vector<float>*, const std::vector<float>*> color_effect(std::string_view mode) {
		size_t dash_pos = mode.find('-');
		if (dash_pos == std::string_view::npos) return {nullptr, nullptr};
		std::string_view left_key = mode.substr(0, dash_pos);
		std::string_view right_key = mode.substr(dash_pos + 1);
		auto left = colors.find(left_key);
		auto right = colors.find(right_key);
		if (left != colors.end() && right != colors.end()) return { &(left->second), &(right->second) };
		return {nullptr, nullptr};
	}

	void modes(int i) {
		auto static_color = colors.count(cfg.mode) ? &colors[cfg.mode] : nullptr;
		auto [color1, color2] = color_effect(cfg.mode);
		if (static_color != nullptr) {
			auto &c = *static_color;
			cfg.r = c[0], cfg.g = c[1], cfg.b = c[2];
		} else if (color1 && color2 ) {
			auto &c1 = *color1;
			auto &c2 = *color2;
			float speed = 50.0f;
			int offset = static_cast<int>(t * speed) % cfg.num_leds;
			if (offset < 0) offset += cfg.num_leds;
			int position = (i - offset + cfg.num_leds) % cfg.num_leds;
			if (position < cfg.num_leds / 2) {
				cfg.r = c1[0]; cfg.g = c1[1]; cfg.b = c1[2];
			} else {
				cfg.r = c2[0]; cfg.g = c2[1]; cfg.b = c2[2];
			}
		} else if (cfg.mode == "rainbow") {
			float hue = fmodf(t + i * 0.05f, 6.0f);
			float x = 255.0f * (1.0f - fabsf(fmodf(hue, 2.0f) - 1.0f));
			if (hue < 1.0f)  { cfg.r = 255; cfg.g = x;   cfg.b = 0; }
			else if (hue < 2.0f) { cfg.r = x;   cfg.g = 255; cfg.b = 0; }
			else if (hue < 3.0f) { cfg.r = 0;   cfg.g = 255; cfg.b = x; }
			else if (hue < 4.0f) { cfg.r = 0;   cfg.g = x;   cfg.b = 255; }
			else if (hue < 5.0f) { cfg.r = x;   cfg.g = 0;   cfg.b = 255; }
			else { cfg.r = 255; cfg.g = 0; cfg.b = x; }
		} else if (cfg.mode == "neon") {
			float wave = (sinf(t + i * 0.1f) + 1.0f) / 2.0f;
			cfg.r = 120 + 135 * sinf(t * 0.5f + i * 0.05f) / 2.0f;
			cfg.b = 255 * wave;
		} else throw std::runtime_error("Unknown mode: " + cfg.mode);
	}

public:
	Effect (Config &c) : cfg(c) {
	}
};

class Device {
	friend class Run;
	Config &cfg;
	int fd = -1;
	std::vector<unsigned char> header;

	void set_interface_attribs(int fd, int speed) {
		termios tty{};
		if (tcgetattr(fd, &tty) != 0) return; // copying current settings from file descriptor in tty
		cfsetospeed(&tty, speed); // output speed
		cfsetispeed(&tty, speed); // input speed
		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // setting strong format size for frame
		tty.c_iflag &= ~IGNBRK; // turning off ignoring disconnect
		tty.c_lflag = 0; // disable echo and canonical mode
		tty.c_oflag = 0; // turning off output processing
		tty.c_cc[VMIN] = 0; // read doesn't block (return immediately)
		tty.c_cc[VTIME] = 5; // timeout for reading 0.5s
		tty.c_cflag |= (CLOCAL | CREAD); // ignoring other console lines and accepting for reading from port
		tty.c_cflag &= ~(PARENB | PARODD); // turning off checking errors
		tty.c_cflag &= ~CSTOPB; // using 1 stop bit
		tty.c_cflag &= ~CRTSCTS; // turning off hardware management
		tcsetattr(fd, TCSANOW, &tty); // apply right now
	}

	void update_header () { // header generating for leds
		int ledcount = cfg.num_leds - 1;
		unsigned char hiByte = (ledcount >> 8) & 0xFF;
		unsigned char loByte = ledcount & 0xFF;
		unsigned char checksum = hiByte ^ loByte ^ 0x55;
		header = {'A', 'd', 'a', hiByte, loByte, checksum};
	}

public:
	Device(Config &c) : cfg(c) {
		fd = open(cfg.port.c_str(), O_RDWR | O_NOCTTY | O_SYNC); // read and write access | ignore terminal control signals | sync output
		if (fd < 0) throw std::runtime_error("Port is not opened: " + cfg.port);
		set_interface_attribs(fd, cfg.speed_port);
		update_header();
	}

	~Device() {
		if (fd >= 0) close(fd);
	}
};

class Run {
	Config config;
	Effect effect;
	Device device;
	std::vector<unsigned char> payload;
	void push_mode () {
		for (int i = 0; i < config.num_leds; ++i) {
			effect.modes(i);
			payload.push_back(static_cast<unsigned char>(config.r));
			payload.push_back(static_cast<unsigned char>(config.g));
			payload.push_back(static_cast<unsigned char>(config.b));
		}
	}

	void send_packet() { // send to strip
		auto res = write(device.fd, device.header.data(), device.header.size());
		res = write(device.fd, payload.data(), payload.size());
		(void)res;
	}

	void letsgo () {
		push_mode();
		send_packet();
		payload.reserve(config.num_leds * 3);
		while (true) {
			payload.clear();
			push_mode();
			send_packet();
			effect.t += config.speed;
			if (effect.t >= 10e37) effect.t = 0.0f;
			usleep(20000); // 20ms = 50 FPS
		}
	}
	public:
	Run() : effect(config), device(config) {
		letsgo ();
	}
};

int main() {
	Run run;
}
