#include <array>
#include <algorithm>
#include <atomic>
#include <alsa/asoundlib.h>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <linux/uinput.h>
#include <optional>
#include <string>
#include <mutex>
#include <thread>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <whisper.h>

namespace {
constexpr std::size_t bits_per_word = sizeof(unsigned long) * 8;
constexpr char keybind_file[] = "keybinds.conf";

struct Keybinds {
    std::vector<std::string> trigger {"RIGHTCTRL"};
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
    if (key == "RIGHTCTRL" || key == "RIGHTCONTROL") return keys_down[KEY_RIGHTCTRL];
    if (key == "ALT") return keys_down[KEY_LEFTALT] || keys_down[KEY_RIGHTALT];
    if (key == "DELETE" || key == "DEL") return keys_down[KEY_DELETE];
    if (key == "ENTER" || key == "RETURN") return keys_down[KEY_ENTER];
    if (key == "ESC" || key == "ESCAPE") return keys_down[KEY_ESC];
    if (key == "SPACE") return keys_down[KEY_SPACE];
    if (key == "TAB") return keys_down[KEY_TAB];
    if (key == "BACKSPACE") return keys_down[KEY_BACKSPACE];
    if (key == "MICMUTE" || key == "MIC_MUTE") return keys_down[KEY_MICMUTE];
    if (key.size() >= 2 && key[0] == 'F') {
        const std::string number = key.substr(1);
        char* end = nullptr;
        const long function_number = std::strtol(number.c_str(), &end, 10);
        static constexpr std::array<unsigned int, 24> function_codes {
            KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
            KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
            KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18,
            KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24,
        };
        if (*end == '\0' && function_number >= 1 && function_number <= 24)
            return keys_down[function_codes[function_number - 1]];
    }
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

std::vector<float> record_while(std::atomic_bool& recording, std::mutex& ready_mutex,
                                std::condition_variable& ready_cv, std::atomic_bool& ready) {
    snd_pcm_t* pcm = nullptr;
    const int open_result = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (open_result < 0) {
        std::fprintf(stderr, "Cannot open microphone: %s\n", snd_strerror(open_result));
        ready_cv.notify_one();
        return {};
    }
    const int config_result = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                                                  SND_PCM_ACCESS_RW_INTERLEAVED, 1,
                                                  WHISPER_SAMPLE_RATE, 1, 500000);
    if (config_result < 0) {
        std::fprintf(stderr, "Cannot configure microphone: %s\n", snd_strerror(config_result));
        snd_pcm_close(pcm); ready_cv.notify_one(); return {};
    }
    {
        std::lock_guard lock(ready_mutex);
        ready = true;
    }
    ready_cv.notify_one();
    std::puts("Recording...");
    std::vector<float> audio;
    std::array<int16_t, 1024> buffer {};
    while (recording) {
        const snd_pcm_sframes_t frames = snd_pcm_readi(pcm, buffer.data(), buffer.size());
        if (frames < 0) {
            const int recover_result = snd_pcm_recover(pcm, frames, 1);
            if (recover_result < 0) {
                std::fprintf(stderr, "Microphone capture failed: %s\n", snd_strerror(recover_result));
                break;
            }
            continue;
        }
        for (snd_pcm_sframes_t i = 0; i < frames; ++i) audio.push_back(buffer[i] / 32768.0f);
    }
    snd_pcm_close(pcm);
    return audio;
}

class VirtualKeyboard {
public:
    VirtualKeyboard() {
        fd_ = open("/dev/uinput", O_WRONLY);
        if (fd_ < 0) {
            std::fprintf(stderr, "Cannot open /dev/uinput; transcription will not be typed: %s\n",
                         std::strerror(errno));
            return;
        }
        if (ioctl(fd_, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd_, UI_SET_EVBIT, EV_SYN) < 0) {
            fail();
            return;
        }
        for (int key = 0; key <= KEY_MAX; ++key)
            if (ioctl(fd_, UI_SET_KEYBIT, key) < 0) {
                fail();
                return;
            }
        uinput_setup setup {};
        std::strncpy(setup.name, "Calliope virtual keyboard", UINPUT_MAX_NAME_SIZE - 1);
        setup.id.bustype = BUS_VIRTUAL;
        if (ioctl(fd_, UI_DEV_SETUP, &setup) < 0 || ioctl(fd_, UI_DEV_CREATE) < 0) {
            fail();
            return;
        }
        active_ = true;
        // Give the desktop input stack time to discover the new device.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ~VirtualKeyboard() {
        if (active_) ioctl(fd_, UI_DEV_DESTROY);
        if (fd_ >= 0) close(fd_);
    }

    void type(const std::string& text) {
        if (!active_) return;
        for (const unsigned char character : text) {
            if (!active_) return;
            const auto key = key_for(character);
            if (!key) continue;
            if (key->shift) emit(KEY_LEFTSHIFT, 1);
            emit(key->code, 1);
            emit(key->code, 0);
            if (key->shift) emit(KEY_LEFTSHIFT, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void paste() {
        if (!active_) return;
        emit(KEY_LEFTCTRL, 1);
        emit(KEY_V, 1);
        emit(KEY_V, 0);
        emit(KEY_LEFTCTRL, 0);
    }

private:
    struct KeyStroke { unsigned int code; bool shift; };

    static std::optional<KeyStroke> key_for(unsigned char character) {
        static constexpr std::array<unsigned int, 26> letter_codes {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
        };
        if (character >= 'a' && character <= 'z') return {{letter_codes[character - 'a'], false}};
        if (character >= 'A' && character <= 'Z') return {{letter_codes[character - 'A'], true}};
        if (character >= '1' && character <= '9')
            return {{static_cast<unsigned int>(KEY_1 + character - '1'), false}};
        if (character == '0') return {{KEY_0, false}};
        switch (character) {
        case ' ': return {{KEY_SPACE, false}}; case '\n': return {{KEY_ENTER, false}};
        case '.': return {{KEY_DOT, false}}; case ',': return {{KEY_COMMA, false}};
        case '?': return {{KEY_SLASH, true}}; case '!': return {{KEY_1, true}};
        case '\'': return {{KEY_APOSTROPHE, false}}; case '"': return {{KEY_APOSTROPHE, true}};
        case '-': return {{KEY_MINUS, false}}; case '_': return {{KEY_MINUS, true}};
        case ':': return {{KEY_SEMICOLON, true}}; case ';': return {{KEY_SEMICOLON, false}};
        case '(': return {{KEY_9, true}}; case ')': return {{KEY_0, true}};
        default: return std::nullopt;
        }
    }

    void emit(unsigned int code, int value) {
        input_event event {};
        event.type = EV_KEY;
        event.code = code;
        event.value = value;
        write_event(event);
        event.type = EV_SYN;
        event.code = SYN_REPORT;
        event.value = 0;
        write_event(event);
    }

    void write_event(const input_event& event) {
        const char* data = reinterpret_cast<const char*>(&event);
        std::size_t remaining = sizeof(event);
        while (remaining != 0) {
            const ssize_t written = write(fd_, data, remaining);
            if (written > 0) {
                data += written;
                remaining -= written;
            } else if (written < 0 && errno == EINTR) {
                continue;
            } else {
                std::fprintf(stderr, "Virtual keyboard write failed: %s\n", std::strerror(errno));
                active_ = false;
                return;
            }
        }
    }

    void fail() {
        std::fprintf(stderr, "Cannot create virtual keyboard: %s\n", std::strerror(errno));
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
    }

    int fd_ = -1;
    bool active_ = false;
};

bool copy_to_clipboard(const std::string& text) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        std::fprintf(stderr, "Cannot create clipboard pipe: %s\n", std::strerror(errno));
        return false;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        std::fprintf(stderr, "Cannot start wl-copy: %s\n", std::strerror(errno));
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }
    if (pid == 0) {
        dup2(pipe_fds[0], STDIN_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execlp("wl-copy", "wl-copy", "--", nullptr);
        _exit(127);
    }

    close(pipe_fds[0]);
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining != 0) {
        const ssize_t written = write(pipe_fds[1], data, remaining);
        if (written > 0) {
            data += written;
            remaining -= written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            std::fprintf(stderr, "Cannot write clipboard contents: %s\n", std::strerror(errno));
            close(pipe_fds[1]);
            waitpid(pid, nullptr, 0);
            return false;
        }
    }
    close(pipe_fds[1]);
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool has_audible_signal(const std::vector<float>& audio) {
    if (audio.size() < WHISPER_SAMPLE_RATE / 4) return false;

    double sum_of_squares = 0.0;
    float peak = 0.0f;
    for (const float sample : audio) {
        sum_of_squares += static_cast<double>(sample) * sample;
        peak = std::max(peak, std::abs(sample));
    }
    const float rms = static_cast<float>(std::sqrt(sum_of_squares / audio.size()));
    return peak >= 0.01f && rms >= 0.002f;
}

void transcribe(const std::vector<float>& audio, VirtualKeyboard& keyboard) {
    if (audio.empty()) { std::fprintf(stderr, "No microphone audio captured.\n"); return; }
    if (!has_audible_signal(audio)) {
        std::fprintf(stderr, "Recording was too short or silent; nothing will be pasted.\n");
        return;
    }
    whisper_context_params context_params = whisper_context_default_params();
    context_params.use_gpu = false;
    whisper_context* context = whisper_init_from_file_with_params(CALLIOPE_MODEL_PATH, context_params);
    if (!context) { std::fprintf(stderr, "Cannot load whisper model: %s\n", CALLIOPE_MODEL_PATH); return; }
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = "en";
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    if (whisper_full(context, params, audio.data(), audio.size()) == 0) {
        std::string text;
        for (int i = 0; i < whisper_full_n_segments(context); ++i)
            text += whisper_full_get_segment_text(context, i);
        std::printf("%s", text.c_str());
        std::puts("");
        if (trim(text).empty()) {
            std::fprintf(stderr, "Transcription was empty; nothing will be pasted.\n");
            whisper_free(context);
            return;
        }
        if (copy_to_clipboard(text)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            keyboard.paste();
        } else {
            std::fprintf(stderr, "Clipboard unavailable; typing transcription directly.\n");
            keyboard.type(text);
        }
    } else std::fprintf(stderr, "Transcription failed.\n");
    whisper_free(context);
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

    VirtualKeyboard virtual_keyboard;

    std::array<bool, KEY_MAX + 1> keys_down {};
    bool trigger_active = false;
    std::atomic_bool recording = false;
    std::thread recorder;
    std::vector<float> audio;
    std::mutex recorder_ready_mutex;
    std::condition_variable recorder_ready_cv;
    std::atomic_bool recorder_ready = false;

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
            recording = false;
            if (recorder.joinable()) recorder.join();
            close(fd);
            return 0;
        }

        const bool trigger_now = combination_is_down(keybinds.trigger, keys_down);
        if (trigger_now && !trigger_active) {
            recording = true;
            recorder_ready = false;
            recorder = std::thread([&] {
                audio = record_while(recording, recorder_ready_mutex, recorder_ready_cv, recorder_ready);
            });
            std::unique_lock lock(recorder_ready_mutex);
            recorder_ready_cv.wait_for(lock, std::chrono::seconds(2), [&] { return recorder_ready.load(); });
        } else if (!trigger_now && trigger_active) {
            recording = false;
            recorder.join();
            transcribe(audio, virtual_keyboard);
        }
        trigger_active = trigger_now;
    }

    recording = false;
    if (recorder.joinable()) recorder.join();
    close(fd);
    return 0;
}
