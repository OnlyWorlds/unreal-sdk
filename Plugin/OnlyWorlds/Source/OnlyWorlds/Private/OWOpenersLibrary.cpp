#include "OWOpenersLibrary.h"

#include "OWAddressDoor.h"
#include "OWConvert.h"
#include "OWOpeners.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

using OWConvert::Str;
using OWConvert::Utf8;

namespace
{
	OnlyWorlds::OWOpenerSurface AsSurface(const FOWOpenerSurface& Surface)
	{
		OnlyWorlds::OWOpenerSurface Out;
		if (!Surface.Tool.IsEmpty()) Out.tool = Utf8(Surface.Tool);
		Out.audience = Surface.Audience == EOWAudience::Member ? OnlyWorlds::OWOpeners::Member : OnlyWorlds::OWOpeners::Public;
		if (Surface.SignIn != EOWSignIn::Unknown) Out.signed_in = Surface.SignIn == EOWSignIn::SignedIn;
		Out.browser = Surface.bBrowser;
		Out.phone = Surface.bPhone;
		return Out;
	}

	bool Answer(const OnlyWorlds::OWOpenersResult& Result, TArray<FOWOpener>& Rows, FString& Reason)
	{
		if (!Result.ok)
		{
			Reason = Str(Result.reason.empty() ? Result.message : Result.reason);
			return false;
		}
		for (const OnlyWorlds::OWOpener& R : Result.rows)
		{
			FOWOpener& Row = Rows.AddDefaulted_GetRef();
			Row.Id = Str(R.id);
			Row.Name = Str(R.name);
			Row.Href = Str(R.href);
			Row.State = R.state ? Str(*R.state) : FString();
		}
		return true;
	}
}

bool UOWOpeners::OpenersFor(const UOWWorld* World, const FString& ElementId, const FOWOpenerSurface& Surface,
	TArray<FOWOpener>& Rows, FString& Reason)
{
	Rows.Reset();
	Reason.Reset();
	OnlyWorlds::OWWorld* Native = World ? World->Native() : nullptr;
	if (!Native)
	{
		Reason = TEXT("no world");
		return false;
	}
	OnlyWorlds::OWAddressParts Address;
	Address.form = OnlyWorlds::OWAddress::Human;
	Address.world = Native->Id();
	const char* Kind = OnlyWorlds::OWOpeners::World;
	if (!ElementId.IsEmpty())
	{
		const OnlyWorlds::OWElement* E = Native->Get(Utf8(ElementId));
		if (!E)
		{
			Reason = FString::Printf(TEXT("no element %s in this world"), *ElementId);
			return false;
		}
		Address.type = E->Type();
		Address.id = E->Id();
		Kind = OnlyWorlds::OWOpeners::Element;
	}
	return Answer(OnlyWorlds::OWOpeners::OpenersFor(OnlyWorlds::OWTools::All(), Kind, Address, AsSurface(Surface)), Rows, Reason);
}

bool UOWOpeners::OpenersForAddress(const FOWAddress& Address, const FOWOpenerSurface& Surface, TArray<FOWOpener>& Rows,
	FString& Reason)
{
	Rows.Reset();
	Reason.Reset();
	if (Address.bHasMoment && !OWAddressDoor::MomentFits(Address.Moment))
	{
		// The typed parts hold 32 bits: the whole address goes to the writer, which names the first refusal, a part
		// before the moment's or bad_moment, as the core's OpenersFor checks the whole first.
		OnlyWorlds::OWJsonValue Whole = OWAddressDoor::AsJson(Address);
		if (Address.Form.IsEmpty()) Whole.Set("form", OnlyWorlds::OWJsonValue::String(std::string(OnlyWorlds::OWAddress::Human)));
		const OnlyWorlds::OWAddressWritten Written = OnlyWorlds::OWAddress::WriteJson(Whole);
		Reason = Str(Written.ok || Written.reason.empty() ? std::string("bad_moment") : Written.reason);
		return false;
	}
	const char* Kind = OWAddressDoor::Given(Address.Id, Address.bHasId) ? OnlyWorlds::OWOpeners::Element : OnlyWorlds::OWOpeners::World;
	return Answer(OnlyWorlds::OWOpeners::OpenersFor(OnlyWorlds::OWTools::All(), Kind, OWAddressDoor::AsParts(Address),
		AsSurface(Surface)), Rows, Reason);
}
