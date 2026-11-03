#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "GameNet.h"
#include "Protocol.h"

// Вся игра живёт в одном потоке (игровом) и ничего не знает про сокеты.

constexpr int   TICK_RATE = 30;
constexpr float DT = 1.0f / TICK_RATE;
constexpr float PLAYER_SPEED = 5.0f;     // единиц в секунду
constexpr float ARENA_HALF = 50.0f;    // арена: квадрат от -50 до 50

constexpr std::uint32_t IDLE_TIMEOUT = 10 * TICK_RATE;   // тиков тишины до отключения
constexpr std::size_t   MAX_INPUT_QUEUE = 16;               // команд ввода в очереди на игрока
constexpr std::size_t   MAX_PLAYERS = 16;

enum Button : std::uint8_t { BTN_UP = 1, BTN_DOWN = 2, BTN_LEFT = 4, BTN_RIGHT = 8 };

struct InputCmd
{
    std::uint32_t seq;
    std::uint8_t  buttons;
};

struct Player
{
    int           user_id = 0;
    std::string   name;
    float         x = 0, y = 0;
    std::uint32_t last_seq = 0;          // последняя обработанная команда (уходит клиенту как ack_seq)
    std::deque<InputCmd> inputs;
    std::uint32_t last_rx_tick = 0;
    bool          kicked = false;        // отключение уже заказано, второй раз не просим
};

class Game : public IGameHandler
{
public:
    explicit Game(IGameNet& net) : net_(net) {}

    // ===== вызовы от сети (в игровом потоке) =====

    void on_join(PlayerId id, int user_id, const std::string& name) override
    {
        if (players_.size() >= MAX_PLAYERS || user_in_game(user_id))
        {
            net_.disconnect(id);         // нет мест или этот пользователь уже в игре
            return;
        }

        Player p;
        p.user_id = user_id;
        p.name = name;
        p.last_rx_tick = tick_;
        players_[id] = std::move(p);

        proto::Writer w;
        w.u8(proto::S2C_WELCOME);
        w.u32(id);
        w.u8(static_cast<std::uint8_t>(TICK_RATE));
        net_.send(id, w.b, true);
    }

    void on_disconnect(PlayerId id) override
    {
        players_.erase(id);
    }

    void on_packet(PlayerId id, const std::uint8_t* data, std::size_t size) override
    {
        auto it = players_.find(id);
        if (it == players_.end() || size == 0) return;

        Player& p = it->second;
        p.last_rx_tick = tick_;

        proto::Reader r(data, size);
        std::uint8_t type = 0;
        r.u8(type);

        switch (type)
        {
        case proto::C2S_INPUT:
        {
            std::uint32_t seq;
            std::uint8_t buttons;
            if (!r.u32(seq) || !r.u8(buttons)) { kick(id, p); return; }

            if (p.inputs.size() >= MAX_INPUT_QUEUE) p.inputs.pop_front();
            p.inputs.push_back({ seq, static_cast<std::uint8_t>(buttons & 0x0F) });
            break;
        }
        case proto::C2S_PING:
        {
            proto::Writer w;
            w.u8(proto::S2C_PONG);
            net_.send(id, w.b, false);
            break;
        }
        default:
            kick(id, p);                 // неизвестный тип (в том числе повторный Hello): строго
        }
    }

    // ===== один тик (раз в 1/TICK_RATE секунды) =====

    void tick()
    {
        ++tick_;

        // 1. Простой
        for (auto& [id, p] : players_)
        {
            if (!p.kicked && tick_ - p.last_rx_tick > IDLE_TIMEOUT)
                kick(id, p);
        }

        // 2. Симуляция: одна команда ввода на игрока за тик
        for (auto& [id, p] : players_)
        {
            if (p.kicked) continue;

            std::uint8_t buttons = 0;                   // нет ввода: стоим на месте
            if (!p.inputs.empty())
            {
                buttons = p.inputs.front().buttons;
                p.last_seq = p.inputs.front().seq;
                p.inputs.pop_front();
            }

            float dx = ((buttons & BTN_RIGHT) ? 1.f : 0.f) - ((buttons & BTN_LEFT) ? 1.f : 0.f);
            float dy = ((buttons & BTN_DOWN) ? 1.f : 0.f) - ((buttons & BTN_UP) ? 1.f : 0.f);
            if (dx != 0.f && dy != 0.f) { dx *= 0.70710678f; dy *= 0.70710678f; }

            p.x = std::clamp(p.x + dx * PLAYER_SPEED * DT, -ARENA_HALF, ARENA_HALF);
            p.y = std::clamp(p.y + dy * PLAYER_SPEED * DT, -ARENA_HALF, ARENA_HALF);
        }

        // 3. Снимки: каждому свой (в нём ack его последней команды)
        const std::uint8_t n = static_cast<std::uint8_t>(players_.size());

        for (auto& [id, p] : players_)
        {
            if (p.kicked) continue;

            proto::Writer w;
            w.u8(proto::S2C_SNAPSHOT);
            w.u32(tick_);
            w.u32(p.last_seq);
            w.u8(n);

            for (auto& [other_id, o] : players_)
            {
                w.u32(other_id);
                w.f32(o.x);
                w.f32(o.y);
            }

            net_.send(id, w.b, false);
        }
    }

private:
    void kick(PlayerId id, Player& p)
    {
        if (p.kicked) return;
        p.kicked = true;
        net_.disconnect(id);             // отложенно: on_disconnect придёт позже, players_ не меняется
    }

    bool user_in_game(int user_id) const
    {
        for (const auto& kv : players_)
            if (kv.second.user_id == user_id) return true;
        return false;
    }

    IGameNet& net_;
    std::unordered_map<PlayerId, Player> players_;
    std::uint32_t tick_ = 0;
};