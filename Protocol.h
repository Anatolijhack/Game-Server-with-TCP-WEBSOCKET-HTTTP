#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Кадр в сети:  [u32 длина, big-endian][данные]
// Данные:       [u8 тип][поля, little-endian]
// Рамку [u32 длина] добавляет и снимает Session (raw_mode); сюда приходят уже только "данные".
//
// Предполагается little-endian процессор (x86, ARM): f32 копируются побайтно.

namespace proto {

    // Должен совпадать с MAX_RAW_FRAME в Session.cpp
    constexpr std::size_t MAX_PACKET = 1024;

    // ---------- клиент -> сервер ----------
    constexpr std::uint8_t C2S_HELLO = 1;   // [токен, ASCII, до 128 байт] (обрабатывает GameGateway)
    constexpr std::uint8_t C2S_INPUT = 2;   // [u32 seq][u8 кнопки]
    constexpr std::uint8_t C2S_PING = 3;   // без полей

    // ---------- сервер -> клиент ----------
    constexpr std::uint8_t S2C_WELCOME = 1;     // [u32 id][u8 частота тика]
    constexpr std::uint8_t S2C_SNAPSHOT = 2;     // [u32 тик][u32 ack_seq][u8 n] + n x ([u32 id][f32 x][f32 y])
    constexpr std::uint8_t S2C_PONG = 3;     // без полей
    constexpr std::uint8_t S2C_KICK = 0x7F;  // [u8 причина], см. KICK_* в GameNet.h

    class Writer {
    public:
        void u8(std::uint8_t v) { b.push_back(v); }

        void u32(std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }

        void f32(float f)
        {
            std::uint32_t v;
            std::memcpy(&v, &f, 4);
            u32(v);
        }

        std::vector<std::uint8_t> b;
    };

    // Все чтения проверяют границы и возвращают false, если данных не хватило.
    class Reader {
    public:
        Reader(const std::uint8_t* d, std::size_t n) : p_(d), end_(d + n) {}

        bool u8(std::uint8_t& v)
        {
            if (p_ + 1 > end_) return false;
            v = *p_++;
            return true;
        }

        bool u32(std::uint32_t& v)
        {
            if (p_ + 4 > end_) return false;
            v = static_cast<std::uint32_t>(p_[0])
                | (static_cast<std::uint32_t>(p_[1]) << 8)
                | (static_cast<std::uint32_t>(p_[2]) << 16)
                | (static_cast<std::uint32_t>(p_[3]) << 24);
            p_ += 4;
            return true;
        }

        bool f32(float& f)
        {
            std::uint32_t v;
            if (!u32(v)) return false;
            std::memcpy(&f, &v, 4);
            return true;
        }

        std::size_t left() const { return static_cast<std::size_t>(end_ - p_); }
        const std::uint8_t* ptr() const { return p_; }

    private:
        const std::uint8_t* p_;
        const std::uint8_t* end_;
    };

} // namespace proto