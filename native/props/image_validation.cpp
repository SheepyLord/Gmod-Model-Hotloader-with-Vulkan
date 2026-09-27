#include "core.hpp"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>
namespace props {
void validatePNG(const Texture& t) {
    int w=0,h=0,c=0;
    auto pixels=stbi_load_from_memory(t.png.data(),int(t.png.size()),&w,&h,&c,4);
    if(!pixels) throw std::runtime_error("Invalid or truncated PNG pixels");
    stbi_image_free(pixels);
    if(w!=int(t.width)||h!=int(t.height))throw std::runtime_error("PNG decoded dimensions mismatch");
}
}
