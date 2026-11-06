// SFZ2A's frame-register/input routine does not sit at the SFA2 rewrite address.
// Keep RegisterGame's registration link, but let the translated ROM code handle it.
namespace cps2recomp {

void RegisterInput() {}

} // namespace cps2recomp
