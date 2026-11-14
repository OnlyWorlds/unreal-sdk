#include "OWAddressLibrary.h"

#include "OWAddressDoor.h"
#include "OWConvert.h"

using OnlyWorlds::OWJsonValue;
using OWConvert::Str;
using OWConvert::Utf8;

OWJsonValue OWAddressDoor::AsJson(const FOWAddress& Address)
{
	OWJsonValue Out = OWJsonValue::Object();
	Out.Set("form", OWJsonValue::String(Utf8(Address.Form)));
	auto Part = [&Out](const char* Key, const FString& Value, bool bHas)
	{
		if (Given(Value, bHas)) Out.Set(Key, OWJsonValue::String(Utf8(Value)));
	};
	Part("world", Address.World, Address.bHasWorld);
	Part("type", Address.Type, Address.bHasType);
	Part("id", Address.Id, Address.bHasId);
	Part("lens", Address.Lens, Address.bHasLens);
	if (Address.bHasMoment) Out.Set("moment", OWJsonValue::Number(static_cast<int64_t>(Address.Moment)));
	return Out;
}

OnlyWorlds::OWAddressParts OWAddressDoor::AsParts(const FOWAddress& Address)
{
	OnlyWorlds::OWAddressParts Out;
	Out.form = Utf8(Address.Form);
	auto Part = [](const FString& Value, bool bHas) -> std::optional<std::string>
	{
		if (!Given(Value, bHas)) return std::nullopt;
		return Utf8(Value);
	};
	Out.world = Part(Address.World, Address.bHasWorld);
	Out.type = Part(Address.Type, Address.bHasType);
	Out.id = Part(Address.Id, Address.bHasId);
	Out.lens = Part(Address.Lens, Address.bHasLens);
	if (Address.bHasMoment) Out.moment = static_cast<int32_t>(Address.Moment);
	return Out;
}

FOWAddress OWAddressDoor::FromReading(const OnlyWorlds::OWAddressReading& Reading)
{
	FOWAddress Out;
	Out.Form = Str(Reading.form);
	auto Part = [](const std::optional<std::string>& Value, FString& To, bool& bHas)
	{
		bHas = Value.has_value();
		To = bHas ? Str(*Value) : FString();
	};
	Part(Reading.world, Out.World, Out.bHasWorld);
	Part(Reading.type, Out.Type, Out.bHasType);
	Part(Reading.id, Out.Id, Out.bHasId);
	Part(Reading.lens, Out.Lens, Out.bHasLens);
	Out.bHasMoment = Reading.moment.has_value();
	Out.Moment = Reading.moment.value_or(0);
	Out.bWorldIsSlug = Reading.world_is_slug;
	Out.bCanonical = Reading.canonical;
	return Out;
}

bool UOWAddresses::ParseAddress(const FString& Text, FOWAddress& Address, FString& Reason)
{
	Address = FOWAddress();
	Reason.Reset();
	const OnlyWorlds::OWAddressReading Reading = OnlyWorlds::OWAddress::Parse(Utf8(Text));
	if (!Reading.ok)
	{
		Reason = Str(Reading.reason);
		return false;
	}
	Address = OWAddressDoor::FromReading(Reading);
	return true;
}

bool UOWAddresses::ReadAddressAt(const FString& Text, int32 Index, FOWAddress& Address, int32& End)
{
	Address = FOWAddress();
	End = 0;
	if (Index < 0 || Index > Text.Len()) return false;
	// The core counts UTF-8 bytes, the FString UTF-16 units: the index is the byte size of the text before it, and End
	// is the unit count of the bytes before the core's end. A head that is not the whole text's own first bytes (an
	// index between the two halves of a surrogate pair) holds no address.
	const std::string Whole = Utf8(Text);
	const std::string Head = Utf8(Text.Left(Index));
	if (Head.size() > Whole.size() || Whole.compare(0, Head.size(), Head) != 0) return false;
	const std::optional<OnlyWorlds::OWAddressFound> Found = OnlyWorlds::OWAddress::ReadAt(Whole, Head.size());
	if (!Found) return false;
	Address = OWAddressDoor::FromReading(Found->reading);
	End = Str(Whole.substr(0, Found->end)).Len();
	return true;
}

bool UOWAddresses::WriteAddress(const FOWAddress& Address, FString& Text, FString& Reason)
{
	Text.Reset();
	Reason.Reset();
	// Through WriteJson, so a moment past 32 bits refuses as bad_moment in the writer's order, as the packages refuse it.
	const OnlyWorlds::OWAddressWritten Written = OnlyWorlds::OWAddress::WriteJson(OWAddressDoor::AsJson(Address));
	if (!Written.ok)
	{
		Reason = Str(Written.reason.empty() ? Written.message : Written.reason);
		return false;
	}
	Text = Str(Written.address);
	return true;
}
