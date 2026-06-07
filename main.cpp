#include <string>
#include <vector>
#include <cmath>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <map>

class Device {
		const std::string config = "/etc/skydimo-control/skydimo-control.conf"; // config path
		std::string port = "/dev/ttyUSB0"; // or your any other device which you can see with command 'lsusb'
		int num_leds = 255; // default value ( i guess )
		int speed_port = 115200; // also default value
		float speed = 0.15f; // default speed
		std::string mode = "rainbow"; // default mode
		int fd = -1;
		float t = 0.0f;
		float r = 0, g = 0, b = 0;
		std::vector<unsigned char> payload, header;

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

		std::vector<float>* static_color = colors.count(mode) ? &colors[mode] : nullptr;

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
			int ledcount = num_leds - 1;
			unsigned char hiByte = (ledcount >> 8) & 0xFF;
			unsigned char loByte = ledcount & 0xFF;
			unsigned char checksum = hiByte ^ loByte ^ 0x55;
			header = {'A', 'd', 'a', hiByte, loByte, checksum};
		}

		void modes(int i) {
			auto [color1, color2] = color_effect(mode);
			if (static_color != nullptr) {
				auto &c = *static_color;
				r = c[0], g = c[1], b = c[2];
			} else if (color1 && color2 ) {
				auto &c1 = *color1;
				auto &c2 = *color2;
				float speed = 50.0f;
				int offset = static_cast<int>(t * speed) % num_leds;
				if (offset < 0) offset += num_leds;
				int position = (i - offset + num_leds) % num_leds;
				if (position < num_leds / 2) {
					r = c1[0]; g = c1[1]; b = c1[2];
				} else {
					r = c2[0]; g = c2[1]; b = c2[2];
				}
			} else if (mode == "rainbow") {
				float hue = fmodf(t + i * 0.05f, 6.0f);
				float x = 255.0f * (1.0f - fabsf(fmodf(hue, 2.0f) - 1.0f));
				if (hue < 1.0f)  { r = 255; g = x;   b = 0; }
				else if (hue < 2.0f) { r = x;   g = 255; b = 0; }
				else if (hue < 3.0f) { r = 0;   g = 255; b = x; }
				else if (hue < 4.0f) { r = 0;   g = x;   b = 255; }
				else if (hue < 5.0f) { r = x;   g = 0;   b = 255; }
				else { r = 255; g = 0;   b = x; }
			} else if (mode == "neon") {
				float wave = (sinf(t + i * 0.1f) + 1.0f) / 2.0f;
				r = 120 + 135 * sinf(t * 0.5f + i * 0.05f) / 2.0f;
				b = 255 * wave;
			} else throw std::runtime_error("Unknown mode: " + mode);
		}

		void push_mode () {
			for (int i = 0; i < num_leds; ++i) {
				modes(i);
				size_t idx = i * 3;
				payload.push_back(static_cast<unsigned char>(r));
				payload.push_back(static_cast<unsigned char>(g));
				payload.push_back(static_cast<unsigned char>(b));
			}
		}

		void send_packet() { // send to strip
			auto res = write(fd, header.data(), header.size());
			res = write(fd, payload.data(), payload.size());
			(void)res;
		}

	public:
		Device() { // constructor
			load_config();
			fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC); // read and write access | ignore terminal control signals | sync output
			if (fd < 0) throw std::runtime_error("Port is not opened: " + port);
			set_interface_attribs(fd, speed_port);
			update_header();
		}

		~Device() { // destructor
			if (fd >= 0) close(fd);
		}

		void run () {
			payload.reserve(num_leds * 3);
			while (true) {
				payload.clear();
				push_mode();
				send_packet();
				t += speed;
				if (t > 1000.0) t = 0.0f;
				usleep(20000); // 20ms = 50 FPS
			}
		}
};

int main() {
	Device device;
	device.run();
}
