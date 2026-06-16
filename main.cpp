#include <string>
#include <vector>
#include <cmath>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <map>
#include <tuple>

class Config {
public:
	const std::string config = "/etc/skydimo-control/skydimo-control.conf"; // config path
	std::string port = "/dev/ttyUSB0"; // or your any other device which you can see with command 'lsusb'
	int num_leds = 65; // led counts
	int packet_buffer_size = 765; // default value for sending data packet with colors
	int speed_port = 115200; // also default value
	float speed = 0.15f; // default speed
	float brightness = 1.0f; // default brightness
	std::string mode = "rainbow"; // default mode
	std::string effect = "static"; // default effect
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
				else if (word == "effect") ss >> effect;
				else if (word == "speed") ss >> speed;
				else if (word == "brightness") { ss >> brightness; if (brightness > 1 ) brightness /= 100; if (brightness > 100) brightness = 1.0f; }
				else if (word == "packet_buffer_size") ss >> packet_buffer_size;
			}
		}
	}
	Config () { load_config(); }
};

class Effect {
	float r = 0, g = 0, b = 0;
	using EffectsFunc = float (*)(float t, int i);
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

	std::map<std::string, EffectsFunc, std::less<>> effects = {
		{"breath", [](float t, int i) {
			return (sinf(t + 0.0f) + 1.0f) / 2.0f;
		}},
		{"pulsar", [](float t, int i) {
			return (sinf(t * 10.0f) + 1.0f) / 2.0f;
		}},
		{"shine", [](float t, int i) {
			return fabsf(sinf(t * 2.0f + i * 0.1f));
		}},
		{"static", [] (float t, int i) { return 1.0f; }}
	};

	auto load_effects(int i) {
		float wave = 0.0f;
		bool exs_effect = false;
		auto color_effect = [&](std::string_view mode) -> std::pair<const std::vector<float>*, const std::vector<float>*> {
			size_t dash_pos = mode.find('-');
			if (dash_pos == std::string_view::npos) return {nullptr, nullptr};
			std::string_view left_key = mode.substr(0, dash_pos);
			std::string_view right_key = mode.substr(dash_pos + 1);
			auto left = colors.find(left_key);
			auto right = colors.find(right_key);
			if (left != colors.end() && right != colors.end()) return { &(left->second), &(right->second) };
			return {nullptr, nullptr};
		};
		auto static_color = colors.count(cfg.mode) ? &colors[cfg.mode] : nullptr;
		auto [color1, color2] = color_effect(cfg.mode);
		auto it = effects.find(cfg.effect);
		if (it != effects.end()) {
			wave = it->second(t, i);
			exs_effect = true;
		} else exs_effect = false;
		return std::make_tuple(static_color, color1, color2, wave, exs_effect);
	}

	void modes(int i) {
		auto [static_color, color1, color2, wave, exs_effect] = load_effects(i);
		if (static_color != nullptr) {
			auto &c = *static_color;
			r = c[0], g = c[1], b = c[2];
			if (exs_effect) {
				r *= wave, g *= wave, b *= wave;
			}
		} else if (color1 && color2 ) {
			int midpoint = cfg.num_leds / 2;
			auto &c = i < midpoint ? *color1 : *color2;
			r = c[0]; g = c[1]; b = c[2];
			if (exs_effect) {
				r *= wave; g *= wave; b *= wave;
			}
		} else if (cfg.mode == "rainbow") {
			float hue = fmodf(t + i * 0.05f, 6.0f);
			float x = 255.0f * (1.0f - fabsf(fmodf(hue, 2.0f) - 1.0f));
			if (hue < 1.0f)  { r = 255; g = x;   b = 0; }
			else if (hue < 2.0f) { r = x;   g = 255; b = 0; }
			else if (hue < 3.0f) { r = 0;   g = 255; b = x; }
			else if (hue < 4.0f) { r = 0;   g = x;   b = 255; }
			else if (hue < 5.0f) { r = x;   g = 0;   b = 255; }
			else { r = 255; g = 0; b = x; }
		} else if (cfg.mode == "neon") {
			float wave = (sinf(t + i * 0.1f) + 1.0f) / 2.0f;
			r = 120 + 135 * sinf(t * 0.5f + i * 0.05f) / 2.0f;
			b = 255 * wave;
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
		int ledcount = cfg.packet_buffer_size / 3 - 1;
		ledcount = ledcount > cfg.num_leds - 1 ? ledcount : cfg.num_leds - 1; // 32-bit variable
		unsigned char hiByte = (ledcount >> 8) & 0xFF; // shift by 8 bits and turn everything to zeros except the last 8 bits (needed for strip > 256 leds, limit - 65536 leds)
		unsigned char loByte = ledcount & 0xFF; // writing the last 8 bits
		unsigned char checksum = hiByte ^ loByte ^ 0x55; // 0x55 - 01010101 (default Adalight checksum validator)
		header = {'A', 'd', 'a', hiByte, loByte, checksum}; // general frame header
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
	std::vector<unsigned char> nulls;
	void push_colors () {
		for (int i = 0; i < config.num_leds; ++i) {
			effect.modes(i);
			if (config.brightness < 1.0f) { // brightness adjustment
				effect.r *= config.brightness;
				effect.g *= config.brightness;
				effect.b *= config.brightness;
			}
			payload.push_back(static_cast<unsigned char>(effect.r));
			payload.push_back(static_cast<unsigned char>(effect.g));
			payload.push_back(static_cast<unsigned char>(effect.b));
		}
	}

	void prepare_nulls () {
		int total_bytes = config.packet_buffer_size;
		int color_bytes = payload.size();
		if (total_bytes > color_bytes) {
			nulls.resize(total_bytes - color_bytes, 0);
		}
	}

	void send_packet() { // send to strip
		auto res = write(device.fd, device.header.data(), device.header.size());
		res = write(device.fd, payload.data(), payload.size());
		if (!nulls.empty()) {
			res = write(device.fd, nulls.data(), nulls.size());
		}
		(void)res;
	}

	void letsgo () {
		payload.reserve(config.num_leds * 3);
		prepare_nulls();
		send_packet();
		while (true) {
			payload.clear();
			push_colors();
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
