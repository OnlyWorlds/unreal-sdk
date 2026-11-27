#include "OWWorldCacheObject.h"

#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWHttpTransport.h"
#include "OWJson.h"
#include "OWWorldObject.h"

#include <string>
#include <utility>

using OWConvert::Str;
using OWConvert::Utf8;

UOWWorldCache* UOWWorldCache::MakeWorldCache(const FOWClientConfig& Config, const FString& WorldId, const FString& WorldName, bool bReadOnly)
{
	UOWWorldCache* Out = NewObject<UOWWorldCache>(GetTransientPackage());
	Out->Take(std::make_unique<OnlyWorlds::OWWorldCache>(Utf8(WorldId), Utf8(WorldName), bReadOnly), Config);
	return Out;
}

UOWWorldCache* UOWWorldCache::OpenWorldCache(const FOWClientConfig& Config, const FString& Path, FString& Error)
{
	Error.Reset();
	std::string Why;
	std::unique_ptr<OnlyWorlds::OWWorldCache> Read = OnlyWorlds::OWWorldCache::ReadFile(OWConvert::Path(Path), &Why);
	if (!Read)
	{
		Error = Str(Why);
		return nullptr;
	}
	UOWWorldCache* Out = NewObject<UOWWorldCache>(GetTransientPackage());
	Out->Take(std::move(Read), Config);
	return Out;
}

void UOWWorldCache::Take(std::unique_ptr<OnlyWorlds::OWWorldCache> InHeld, const FOWClientConfig& Config)
{
	Held = std::move(InHeld);
	// The transport is chosen when the client is built (SetTransport may come first).
	Core = OWClientShared::CoreConfig(Config, nullptr);
	Core.transport.reset();
	Sender.reset();
	RefreshWorld();
}

bool UOWWorldCache::WriteFile(const FString& Path, FString& Error) const
{
	Error.Reset();
	std::string Why;
	if (Held->WriteFile(OWConvert::Path(Path), &Why)) return true;
	Error = Str(Why);
	return false;
}

void UOWWorldCache::SetTransport(std::shared_ptr<OnlyWorlds::OWTransport> InTransport)
{
	Core.transport = std::move(InTransport);
	Sender.reset();
}

OnlyWorlds::OWClient& UOWWorldCache::Client()
{
	if (!Sender)
	{
		OnlyWorlds::OWClientConfig Config = Core;
		if (!Config.transport) Config.transport = FOWHttpTransport::Make();
		Sender = std::make_unique<OnlyWorlds::OWClient>(std::move(Config));
	}
	return *Sender;
}

void UOWWorldCache::RefreshWorld()
{
	if (!View) View = NewObject<UOWWorld>(this);
	View->SetNative(Held->ToWorld());
}

UOWWorld* UOWWorldCache::GetHeldWorld() const { return View; }
FString UOWWorldCache::GetWorldId() const { return Str(Held->world_id); }
FString UOWWorldCache::GetWorldName() const { return Str(Held->world_name); }
bool UOWWorldCache::IsReadOnly() const { return Held->read_only; }
bool UOWWorldCache::IsSyncing() const { return Held->Syncing(); }
int32 UOWWorldCache::GetElementCount() const { return static_cast<int32>(Held->Count()); }

TArray<FString> UOWWorldCache::GetElementIds(const FString& Type) const
{
	TArray<FString> Out;
	const std::vector<const OnlyWorlds::OWCachedElement*> Of = Type.IsEmpty() ? Held->Elements() : Held->OfType(Utf8(Type));
	Out.Reserve(static_cast<int32>(Of.size()));
	for (const OnlyWorlds::OWCachedElement* E : Of) Out.Add(Str(E->id));
	return Out;
}

bool UOWWorldCache::HasElement(const FString& Id) const { return Held->Has(Utf8(Id)); }

FString UOWWorldCache::GetElementType(const FString& Id) const
{
	const OnlyWorlds::OWCachedElement* E = Held->Get(Utf8(Id));
	return E ? Str(E->type) : FString();
}

FString UOWWorldCache::GetElementJson(const FString& Id) const
{
	const OnlyWorlds::OWCachedElement* E = Held->Get(Utf8(Id));
	return E ? Str(OnlyWorlds::OWJson::Stringify(E->body, /*keep_number_text=*/true)) : FString();
}

bool UOWWorldCache::IsKnownOf(const FString& Id) const { return Held->IsKnownOf(Utf8(Id)); }
int64 UOWWorldCache::GetCursor() const { return Held->Cursor(); }
FString UOWWorldCache::GetCursorToken() const { return Str(Held->CursorToken()); }

int64 UOWWorldCache::GetEpoch(bool& bHasEpoch) const
{
	bHasEpoch = Held->Epoch().has_value();
	return Held->Epoch().value_or(0);
}

FString UOWWorldCache::GetSyncedAt() const { return Str(Held->SyncedAt()); }
