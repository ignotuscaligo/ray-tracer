#include "MaterialLibrary.h"

#include "Color.h"
#include "DiffuseMaterial.h"

MaterialLibrary::MaterialLibrary()
    : Library<Material>()
{
    add(std::make_shared<DiffuseMaterial>("Default"));
    add(std::make_shared<DiffuseMaterial>("White", Color(1.0f, 1.0f, 1.0f)));
    add(std::make_shared<DiffuseMaterial>("Black", Color(0.0f, 0.0f, 0.0f)));
    add(std::make_shared<DiffuseMaterial>("Red", Color(1.0f, 0.0f, 0.0f)));
    add(std::make_shared<DiffuseMaterial>("Yellow", Color(1.0f, 1.0f, 0.0f)));
    add(std::make_shared<DiffuseMaterial>("Green", Color(0.0f, 1.0f, 0.0f)));
    add(std::make_shared<DiffuseMaterial>("Cyan", Color(0.0f, 1.0f, 1.0f)));
    add(std::make_shared<DiffuseMaterial>("Blue", Color(0.0f, 0.0f, 1.0f)));
    add(std::make_shared<DiffuseMaterial>("Magenta", Color(1.0f, 0.0f, 1.0f)));
}
