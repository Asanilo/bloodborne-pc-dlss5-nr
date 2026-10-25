// CPU replay of the production offline camera against mapped test objects.
// No guest executable, game, network module or GPU is started.
#include "gpu/shim/bbport_cinematic.cpp"
#include <cassert>
#include <thread>

static std::array<u8, 0x100> render_camera{}, director{};
static unsigned writes;
extern "C" int runtime_memory_is_mapped(uintptr_t address, uint64_t size) {
    for (auto* object : {&render_camera, &director}) {
        const auto start = reinterpret_cast<uintptr_t>(object->data());
        if (address >= start && address <= start + object->size() &&
            size <= start + object->size() - address)
            return 1;
    }
    return 0;
}
extern "C" void runtime_memory_note_write(uintptr_t, uint64_t) { ++writes; }
extern "C" void bbgpu_cinematic_input_active(int) {}
extern "C" void bbgpu_add_event_filter(BbGpuEventFilter) {}
namespace Game {
const Profile* Active() { return nullptr; }
}
namespace Core {
bool InstallGuestCodeHook(void*, std::span<const std::uint8_t>, std::uint64_t,
                          GuestCodeHook, const std::uint8_t*) { return false; }
}

int main() {
    using namespace BbCinematic;
    const u64 address = reinterpret_cast<u64>(director.data());
    const u64 render = reinterpret_cast<u64>(render_camera.data());
    const std::array<float, 16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,1,2,3,1};
    assert(WriteBytes(render+0x10,matrix.data(),sizeof(matrix)));
    assert(WriteValue<float>(render,0x50,1.0f));
    assert(WriteValue<u64>(address,0x10,render));
    const auto original_render = render_camera;
    Core::GuestRegisterSnapshot registers{};registers.rdi=address;
    Core::BbPortCinematic::CinematicForceOn();
    CameraHook(0,&registers);
    assert(bbsc_cine.on);
    assert(ReadValue<s32>(address,0xC4)==1);
    const auto debug = ReadValue<u64>(address,0xD0);
    assert(debug==reinterpret_cast<u64>(storage.data()));
    assert(ReadValue<float>(debug,0x10+12*sizeof(float))==1.0f);
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_E;
    assert(Core::BbPortCinematic::CinematicHandleEvent(event,nullptr));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CameraHook(0,&registers);
    assert(ReadValue<float>(debug,0x10+13*sizeof(float))>2.0f);
    assert(render_camera==original_render); // hunter/game camera was not moved
    Core::BbPortCinematic::CinematicForceOff();CameraHook(0,&registers);
    assert(!bbsc_cine.on && ReadValue<s32>(address,0xC4)==0);
    assert(ReadValue<u64>(address,0xD0)==0);
    // A stale render pointer must relinquish input, never dereference it.
    assert(WriteValue<u64>(address,0x10,0x1234));
    Core::BbPortCinematic::CinematicForceOn();CameraHook(0,&registers);
    assert(!bbsc_cine.on && !Core::BbPortCinematic::TakeCinematicInput().enabled);
    assert(writes>0);
    std::puts("PASS: offline camera movement, game-camera isolation, pointer restoration and stale mapping exit");
}
