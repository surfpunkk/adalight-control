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
				else if (word == "packet_buffer_size") ss >> packet_buffer_size;
				else if (word == "speed_port") ss >> speed_port;
				else if (word == "speed") ss >> speed;
				else if (word == "brightness") { ss >> brightness; if (brightness > 1 ) brightness /= 100; if (brightness > 100) brightness = 1.0f; }
				else if (word == "mode") ss >> mode;
				else if (word == "effect") ss >> effect;
			}
		}
	}
	Config () { load_config(); }
};

class Effect {
	friend class Run;
	float r = 0, g = 0, b = 0;
	using EffectResult = std::pair<const float*, size_t>;
	using EffectsFunc = EffectResult(*)(float t, int i);
	Config &cfg;
	float t = 0.0f;
	std::map <std::string, std::vector<float>, std::less<>> colors = { // colors base
		{"red", {255, 0, 0}},
		{"green", {0, 255, 0}},
		{"blue", {0, 0, 255}},
		{"azure", {0, 191, 255}},
		{"yellow", {255, 255, 0}},
		{"orange", {255, 165, 0}},
		{"cyan", {0, 255, 255}},
		{"pink", {255, 56, 91}},
		{"purple", {128, 0, 128}},
		{"turquoise", {0, 255, 215}},
		{"white", {255, 255, 255}},
		{"brown", {160, 82, 45}},
	};

	std::map<std::string, EffectsFunc, std::less<>> effects = {
		{"breath", [](float t, int i) -> EffectResult {
			static float wave[1];
			wave[0]=(sinf(t + 0.0f) + 1.0f) / 2.0f;
			return {wave, 1};
		}},
		{"pulsar", [](float t, int i) -> EffectResult {
			static float wave[1];
			wave[0] = (sinf(t * 10.0f) + 1.0f) / 2.0f;
			return {wave, 1};
		}},
		{"shine", [](float t, int i) -> EffectResult {
			static float wave[1];
			wave[0] = fabsf(sinf(t * 2.0f + i * 0.1f));
			return {wave, 1};
		}},
		{"forward_breathing", [](float t, int i) -> EffectResult {
			static float wave[2];
			wave[0] = sinf(t * 0.5f + i * 0.05f) / 2.0f;
			wave[1] = (sinf(t + i * 0.1f) + 1.0f) / 2.0f;
			return {wave, 2};
		}},
		{"static", [] (float t, int i) -> EffectResult {
				static  float wave [1];
				wave[0] = 1.0f;
				return {wave, 1};
		}}
	};

	std::pair<const std::vector<float>*, const std::vector<float>*> what_colors(std::string_view mode) { // color parsing
		size_t dash_pos = mode.find('-');
		if (dash_pos == std::string_view::npos) return {nullptr, nullptr};
		std::string_view left_key = mode.substr(0, dash_pos);
		std::string_view right_key = mode.substr(dash_pos + 1);
		auto left = colors.find(left_key);
		auto right = colors.find(right_key);
		if (left != colors.end() && right != colors.end()) return { &(left->second), &(right->second) };
		return {nullptr, nullptr};
	}

	EffectResult load_effect (const std::string& effect, int i) {
		auto it = effects.find(effect);
		if (it != effects.end()) return it->second(t, i);
		return { nullptr, 0};
	}

	auto load_user_settings(int i) { // take user settings
		float wave = 0.0f;
		bool exs_effect = false;
		auto static_color = colors.count(cfg.mode) ? &colors[cfg.mode] : nullptr;
		auto [effect, size] = load_effect(cfg.effect, i);
		if (effect != nullptr) {
			wave = effect[0];
			exs_effect = true;
		}
		return std::make_tuple(static_color, wave, exs_effect);
	}

	void load_color (std::string color) {
		auto wcolor = &colors[color];
		auto &c = *wcolor;
		r = c[0], g = c[1], b = c[2];
	}

	int movement (int size, int i) {
		float local_speed = cfg.speed * 100.0f;
		float offset = t * local_speed;
		int seg_size = cfg.num_leds / size;
		return static_cast<int>(i + offset) / seg_size % size;
	}

	void modes(int i) {
		auto [static_color, wave, exs_effect] = load_user_settings(i);
		if (static_color != nullptr) {
			auto &c = *static_color;
			r = c[0], g = c[1], b = c[2];
			if (exs_effect) {
				r *= wave, g *= wave, b *= wave;
			}
		} else if (cfg.mode.find('-') != std::string::npos) {
			auto [color1, color2] = what_colors(cfg.mode);
			if (color1 && color2 ) {
				const int midpoint = cfg.num_leds / 2;
				auto &c = i < midpoint ? *color1 : *color2;
				r = c[0]; g = c[1]; b = c[2];
				if (exs_effect) {
					r *= wave; g *= wave; b *= wave;
				}
			} else throw std::runtime_error("Unknown colors: " + cfg.mode);
		} else if (cfg.mode == "rainbow") {
			static const std::string seq[] = {
				"red", "orange", "yellow", "green", "azure", "blue", "purple"
			};
			load_color(seq[movement(std::size(seq), i)]);
		} else if (cfg.mode == "neon") {
			auto [wave, size] = load_effect("forward_breathing", i);
			r = 120 + 135 * wave[0];
			b = 255 * wave[1];
		} else if (cfg.mode == "google") {
			static const std::string seq[] = { "red", "yellow", "green", "blue" };
			load_color(seq[movement(std::size(seq), i)]);
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
	static constexpr float float_limit = 10e37f;
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
			if (effect.t >= float_limit) effect.t = 0.0f;
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