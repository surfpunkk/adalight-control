#include <string>
#include <vector>
#include <cmath>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

class Device {
		const std::string config = "/etc/skydimo-control/skydimo-control.conf";
		std::string port = "/dev/ttyUSB0"; // or your any other device which you can see with command 'lsusb'
		int num_leds = 255; // default value ( i guess )
		int speed_port = 115200; // also default value
		float speed = 0.15f; // default speed
		std::string mode = "rainbow"; // default value
		int fd;
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
	public:
		Device() {
			load_config();
			fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC); // read and write access | ignore terminal control signals | sync output
			set_interface_attribs(fd, speed_port);
		}
		~Device() {
			close(fd);
		}
		int get_num_leds() const {return num_leds;}
		std::string get_mode() const {return mode;}
		float get_speed() const {return speed;}
		int get_fd() const {return fd;}
		void load_config() {
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
};

int main() {
	Device device;
	int num_leds = device.get_num_leds();
	std::string mode = device.get_mode();
	float speed = device.get_speed();
	int fd = device.get_fd();
    float t = 0;
    std::vector<unsigned char> header = {'A', 'd', 'a', 0x00, 0xFF, 0xAA}; // Adalight protocol frame header for 255 leds
    while (true) {
        std::vector<unsigned char> payload;
        payload.reserve(num_leds * 3);

        for (int i = 0; i < num_leds; ++i) {  // frame generating
            float r = 0, g = 0, b = 0;
            if (mode == "rainbow") {
            	float hue = fmodf(t + i * 0.05f, 6.0f);
            	float x = 255.0f * (1.0f - fabsf(fmodf(hue, 2.0f) - 1.0f));
            		if (hue < 1.0f)  { r = 255; g = x;   b = 0; }
            		else if (hue < 2.0f) { r = x;   g = 255; b = 0; }
            		else if (hue < 3.0f) { r = 0;   g = 255; b = x; }
            		else if (hue < 4.0f) { r = 0;   g = x;   b = 255; }
            		else if (hue < 5.0f) { r = x;   g = 0;   b = 255; }
	           		else { r = 255; g = 0;   b = x; }
   			} else if (mode == "red") r = 255;
        	else if (mode == "blue") b = 255;
        	else if (mode == "green") g = 255;
        	else if (mode == "purple") r = 128, b = 255;
        	else if (mode == "yellow") r = 255, g = 255;
        	else if (mode == "orange") r = 255, g = 140;
        	else if (mode == "turquoise") g = 255, b = 215;
        	else if (mode == "neon") {
        		float wave = (sin(t + i * 0.1f) + 1.0f) / 2.0f;
        		r = 120 + 135 * sin(t * 0.5f + i * 0.05f) / 2.0f;
        		b = 255 * wave;
			} else return 1;
            payload.push_back(static_cast<unsigned char>(r));
            payload.push_back(static_cast<unsigned char>(g));
            payload.push_back(static_cast<unsigned char>(b));
        }
		ssize_t res;
		res = write(fd, header.data(), header.size());
		res = write(fd, payload.data(), payload.size());
		(void)res;
        t += speed;
    	if (t > 1000.0) t = 0.0f;

        usleep(20000); // 20ms = 50 FPS
    }
    return 0;
}
