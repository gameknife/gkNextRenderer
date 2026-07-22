
#define STB_IMAGE_IMPLEMENTATION
#include "Engine/Utilities/StbImage.hpp"

// Single stb_image_write implementation for the whole engine (ScreenShot, tinygltf, ...).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
