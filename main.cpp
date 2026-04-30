#include <string>
#include <vector>
#include <cmath>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

void set_interface_attribs(int fd, int speed) {
    struct termios tty; 
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

int main(int argc, char* argv[]) {
    const char* port = "/dev/ttyUSB0"; // or your any other device which you can see with command 'lsusb'
    int num_leds = 255; // default value ( i guess )
    int speed_baud = B115200; // also default value
    std::string mode = (argc > 1) ? (argv[1]) : "neon"; 
    float speed_step = (argc > 2) ? std::stof(argv[2]) : 0.15f;
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC); // read and write access | ignore terminal control signals | sync output 
    set_interface_attribs(fd, speed_baud); // set port speed

    float t = 0;
    std::vector<unsigned char> header = {'A', 'd', 'a', 0x00, 0xFF, 0xAA}; // Adalight protocol frame header for 255 leds

    while (true) {
        std::vector<unsigned char> payload;
        payload.reserve(num_leds * 3);

        for (int i = 0; i < num_leds; ++i) {
            float r = 0, g = 0, b = 0;
            if (mode == "rainbow") {
   	      // to be continued...
            } else if (mode == "red") r = 255;
	      else if (mode == "blue") b = 255;
	      else if (mode == "green") g = 255;
	      else if (mode == "purple") r = 128, b = 255;
	      else if (mode == "yellow") r = 255, g = 255;
              else if (mode == "orange") r = 255, g = 140;
	      else if (mode == "turquoise") g = 255, b = 215;
	      else if (mode == "neon") {
		float wave = (sin(t + i * 0.1f) + 1.0f) / 2.0f;
                r = (100 + 50 * sin(t * 0.5f + i * 0.05f)) / 2;
                b = 150 * wave;
	    } else return 1;
            payload.push_back(static_cast<unsigned char>(r));
            payload.push_back(static_cast<unsigned char>(g));
            payload.push_back(static_cast<unsigned char>(b));
        }
	ssize_t res;
	res = write(fd, header.data(), header.size());
	res = write(fd, payload.data(), payload.size());
	(void)res;
        t += speed_step;

        usleep(20000); // 20ms = 50 FPS
    }

    close(fd);
    return 0;
}
