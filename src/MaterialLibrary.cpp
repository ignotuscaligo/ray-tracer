#include "MaterialLibrary.h"

#include "Color.h"
#include "LambertianMaterial.h"

MaterialLibrary::MaterialLibrary()
    : Library<Material>()
{
    add(std::make_shared<LambertianMaterial>("Default"));
    add(std::make_shared<LambertianMaterial>("White", Color(1.0f, 1.0f, 1.0f)));
    add(std::make_shared<LambertianMaterial>("Black", Color(0.0f, 0.0f, 0.0f)));
    add(std::make_shared<LambertianMaterial>("Red", Color(1.0f, 0.0f, 0.0f)));
    add(std::make_shared<LambertianMaterial>("Yellow", Color(1.0f, 1.0f, 0.0f)));
    add(std::make_shared<LambertianMaterial>("Green", Color(0.0f, 1.0f, 0.0f)));
    add(std::make_shared<LambertianMaterial>("Cyan", Color(0.0f, 1.0f, 1.0f)));
    add(std::make_shared<LambertianMaterial>("Blue", Color(0.0f, 0.0f, 1.0f)));
    add(std::make_shared<LambertianMaterial>("Magenta", Color(1.0f, 0.0f, 1.0f)));
}
