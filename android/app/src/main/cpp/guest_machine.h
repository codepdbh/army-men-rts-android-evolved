#pragma once
#include <string>
#include "pe_image.h"
#include <android/native_window.h>
#include "guest_run_control.h"
#include <memory>

// Run the first guest instructions until an unsupported import/instruction or
// timeout. This is a diagnostic, not the gameplay launch path.
std::string probeGuestBoot(PeImage& image, const PeImageInfo& info,ANativeWindow* window=nullptr,
                          std::shared_ptr<GuestRunControl> control=nullptr);
