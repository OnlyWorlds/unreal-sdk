#pragma once
// Private to the core: the views reader over a table given by the caller, and the generated tables OWViews.cpp looks
// types up in. ViewIn is for the conformance fixture's own tables (one of them at fault on purpose, two rows on one
// kind), which the core's and the plugin's tests read; it is the Unity SDK's internal ViewOf overload and the Godot
// SDK's _view_in, not part of the surface.

#include "OWJson.h"
#include "OWViews.h"

#include <string>
#include <utility>
#include <vector>

namespace OnlyWorlds {
namespace OWViews {

// ViewOf against a given table: `banners` and `rows` in views.json's shape. The answer's row points into `rows`.
OWView ViewIn(const std::vector<OWViewBanner>& banners, const std::vector<OWViewRow>& rows, const OWJsonValue& element);

// The generated tables (OWViews.generated.cpp), each type's slug as the schema writes it, in the schema's type order.
const std::vector<std::pair<std::string, std::vector<std::string>>>& SectionTable();
const std::vector<std::pair<std::string, std::vector<OWViewGenericLink>>>& GenericTable();

}  // namespace OWViews
}  // namespace OnlyWorlds
