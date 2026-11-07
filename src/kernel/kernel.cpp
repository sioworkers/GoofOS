extern "C" void kernel_main() {
    auto* video = reinterpret_cast<volatile unsigned short*>(0xB8000);
    constexpr char message[] = "Hello from Kernel!";

    for (unsigned int i = 0; i < sizeof(message) - 1; ++i) {
        video[80 + i] = static_cast<unsigned short>(0x0F00 | message[i]);
    }

    for (;;) {
        asm volatile("hlt");
    }
}
