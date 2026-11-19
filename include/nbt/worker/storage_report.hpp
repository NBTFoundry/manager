#ifndef NBT_WORKER_STORAGE_REPORT_HPP
#define NBT_WORKER_STORAGE_REPORT_HPP

namespace nbt {
namespace worker {

/* One debug log line on the filesystem holding `root`: type, device, mount
 * point, real path, free and total space, and whether /var is a link. */
void logStorage(const char *root);

} // namespace worker
} // namespace nbt

#endif
