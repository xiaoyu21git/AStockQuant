#pragma once
// ---------------------------------------------------------------------------
// TypedEvent.hpp -- StrongEvent template (pure C++, zero Qt)
//
// Domain layer sees only strongly-typed Payload structs.
// EventFormat string keys are encapsulated inside pack/unpack.
// ---------------------------------------------------------------------------

#include "Event/EventFormat.hpp"
#include <string_view>
#include <utility>

namespace domain::trading {

template <typename Tag>
class StrongEvent final {
public:
    using Payload = typename Tag::Payload;

    StrongEvent() = default;

    explicit StrongEvent(const engine::EventFormat& raw) : m_raw(raw) {
        Tag::unpack(raw, m_payload);
    }

    explicit StrongEvent(Payload payload) : m_payload(std::move(payload)) {
        m_raw = Tag::pack_to_format(m_payload);
    }

    [[nodiscard]] const Payload& payload() const noexcept { return m_payload; }
    [[nodiscard]] Payload& payload() noexcept { return m_payload; }
    [[nodiscard]] const engine::EventFormat& raw() const noexcept { return m_raw; }

    [[nodiscard]] static engine::EventFormat toEventFormat(const Payload& p) {
        return Tag::pack_to_format(p);
    }

    [[nodiscard]] static StrongEvent fromEventFormat(const engine::EventFormat& raw) {
        StrongEvent e;
        e.m_raw = raw;
        Tag::unpack(raw, e.m_payload);
        return e;
    }

private:
    engine::EventFormat m_raw;
    Payload m_payload{};
};

} // namespace domain::trading