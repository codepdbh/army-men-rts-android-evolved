#pragma once
#include <string>
#include "pe_image.h"

// Run the first guest instructions until an unsupported import/instruction or
// timeout. This is a diagnostic, not the gameplay launch path.
std::string probeGuestBoot(PeImage& image, const PeImageInfo& info);
