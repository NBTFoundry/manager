#ifndef NBT_PLUGIN_HMI_THREADS_HPP
#define NBT_PLUGIN_HMI_THREADS_HPP

namespace nbt {
namespace plugin {

// Writes every thread of the HMI process (the plugin runs in it) to the debug
// log: id, name, state, the object it is blocked on with that object's two
// words (a mutex's count and owner), how long it has been blocked, its CPU
// time, its code address, and the code addresses on its stack, each as
// object+offset. For a hang: what spins, what waits on what, who owns it.
// Reads the process's own /proc entry; stops nothing. No heap.
void logHmiThreads(const char *why);

// Names the calling thread (shown in the snapshot and the watchdog's reports).
void nameThread(const char *name);

} // namespace plugin
} // namespace nbt

#endif
