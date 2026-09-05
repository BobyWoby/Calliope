#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <optional>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr std::size_t bits_per_word = sizeof(unsigned long) * 8;
constexpr char keybind_file[] = "keybinds.conf";

struct Keybinds {
    std::vector<std::string> trigger {"MOD", "SHIFT", "V"};
    std::vector<std::string> exit {"MOD", "DELETE"};
};

bool test_bit(const unsigned long* bits, unsigned int bit) {
    return (bits[bit / bits_per_word] & (1UL << (bit % bits_per_word))) != 0;
}

bool is_keyboard(int fd) {
    unsigned long event_bits[(EV_MAX / bits_per_word) + 1] = {};
    unsigned long key_bits[(KEY_MAX / bits_per_word) + 1] = {};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits) < 0 ||
        !test_bit(event_bits, EV_KEY)) return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) return false;

    return test_bit(key_bits, KEY_Q) && test_bit(key_bits, KEY_V) &&
           test_bit(key_bits, KEY_DELETE);
}

int find_keyboard_index() {
    for (int index = 0; index < 32; ++index) {
        const std::string path = "/dev/input/event" + std::to_string(index);
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        const bool keyboard = is_keyboard(fd);
        close(fd);
        if (keyboard) return index;
    }
    return -1;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::vector<std::string>> parse_combination(const std::string& value) {
    std::vector<std::string> keys;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find('+', start);
        std::string key = trim(value.substr(start, end - start));
        for (char& character : key)
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        if (key.empty()) return std::nullopt;
        keys.push_back(key);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return keys;
}

// This intentionally reads the file for every keyboard event, allowing live edits.
Keybinds load_keybinds() {
    Keybinds keybinds;
    std::ifstream file(keybind_file);
    std::string line;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;

        std::string name = trim(line.substr(0, equals));
        for (char& character : name)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        const auto combination = parse_combination(line.substr(equals + 1));
        if (!combination) continue;

        if (name == "trigger") keybinds.trigger = *combination;
        if (name == "exit") keybinds.exit = *combination;
    }
    return keybinds;
}

bool key_is_down(const std::string& key, const std::array<bool, KEY_MAX + 1>& keys_down) {
    if (key == "MOD") return keys_down[KEY_LEFTMETA] || keys_down[KEY_RIGHTMETA];
    if (key == "SHIFT") return keys_down[KEY_LEFTSHIFT] || keys_down[KEY_RIGHTSHIFT];
    if (key == "CTRL" || key == "CONTROL")
        return keys_down[KEY_LEFTCTRL] || keys_down[KEY_RIGHTCTRL];
    if (key == "ALT") return keys_down[KEY_LEFTALT] || keys_down[KEY_RIGHTALT];
    if (key == "DELETE" || key == "DEL") return keys_down[KEY_DELETE];
    if (key == "ENTER" || key == "RETURN") return keys_down[KEY_ENTER];
    if (key == "ESC" || key == "ESCAPE") return keys_down[KEY_ESC];
    if (key == "SPACE") return keys_down[KEY_SPACE];
    if (key == "TAB") return keys_down[KEY_TAB];
    if (key == "BACKSPACE") return keys_down[KEY_BACKSPACE];
    if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
        // Linux evdev letter key codes are not in alphabetical order.
        static constexpr std::array<unsigned int, 26> letter_codes {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        };
        return keys_down[letter_codes[key[0] - 'A']];
    }
    if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
        const unsigned int code = key[0] == '0' ? KEY_0 : KEY_1 + (key[0] - '1');
        return keys_down[code];
    }
    return false;
}

bool combination_is_down(const std::vector<std::string>& combination,
                         const std::array<bool, KEY_MAX + 1>& keys_down) {
    if (combination.empty()) return false;
    for (const std::string& key : combination)
        if (!key_is_down(key, keys_down)) return false;
    return true;
}
} // namespace

int main() {
    const int keyboard_index = find_keyboard_index();
    if (keyboard_index < 0) {
        std::fprintf(stderr, "No keyboard input device found.\n");
        return 1;
    }

    const std::string path = "/dev/input/event" + std::to_string(keyboard_index);
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::fprintf(stderr, "Could not open %s: %s\n", path.c_str(), std::strerror(errno));
        return 1;
    }
    std::printf("Listening on %s\n", path.c_str());

    std::array<bool, KEY_MAX + 1> keys_down {};
    bool trigger_active = false;

    input_event event {};
    while (true) {
        const ssize_t bytes_read = read(fd, &event, sizeof(event));
        if (bytes_read == -1 && errno == EINTR) continue;
        if (bytes_read != sizeof(event)) {
            if (bytes_read < 0)
                std::fprintf(stderr, "Keyboard read failed: %s\n", std::strerror(errno));
            break;
        }
        if (event.type != EV_KEY) continue;

        if (event.code > KEY_MAX) continue;
        keys_down[event.code] = event.value != 0; // Includes key-repeat events.

        const Keybinds keybinds = load_keybinds();
        if (event.value != 0 && combination_is_down(keybinds.exit, keys_down)) {
            std::puts("Exiting.");
            close(fd);
            return 0;
        }

        const bool trigger_now = combination_is_down(keybinds.trigger, keys_down);
        if (trigger_now && !trigger_active) std::puts("Trigger");
        trigger_active = trigger_now;
    }

    close(fd);
    return 0;
}
