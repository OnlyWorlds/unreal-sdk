#include "OWWorldObject.h"

#include "OWConvert.h"
#include "OWFiles.h"
#include "OWJson.h"

using OWConvert::Str;
using OWConvert::Utf8;

void UOWWorld::SetNative(std::unique_ptr<OnlyWorlds::OWWorld> InWorld)
{
	World = std::move(InWorld);
}

FString UOWWorld::GetId() const
{
	return World ? Str(World->Id()) : FString();
}

FString UOWWorld::GetWorldName() const
{
	if (!World) return FString();
	const OnlyWorlds::OWJsonValue* Name = World->info.Find("name");
	return Name && Name->IsString() ? Str(Name->Text()) : FString();
}

FString UOWWorld::GetRoot() const
{
	return World ? Str(World->root) : FString();
}

int32 UOWWorld::GetElementCount() const
{
	return World ? static_cast<int32>(World->elements.size()) : 0;
}

TArray<FString> UOWWorld::GetElementIds(const FString& Type) const
{
	TArray<FString> Out;
	if (!World) return Out;
	const std::string T = Utf8(Type);
	for (const OnlyWorlds::OWElement& E : World->elements)
	{
		if (T.empty() || E.Type() == T) Out.Add(Str(E.Id()));
	}
	return Out;
}

bool UOWWorld::HasElement(const FString& Id) const
{
	return World && World->Get(Utf8(Id)) != nullptr;
}

FString UOWWorld::GetElementType(const FString& Id) const
{
	const OnlyWorlds::OWElement* E = World ? World->Get(Utf8(Id)) : nullptr;
	return E ? Str(E->Type()) : FString();
}

FString UOWWorld::GetElementName(const FString& Id) const
{
	const OnlyWorlds::OWElement* E = World ? World->Get(Utf8(Id)) : nullptr;
	return E ? Str(E->Name()) : FString();
}

FString UOWWorld::GetElementJson(const FString& Id) const
{
	const OnlyWorlds::OWElement* E = World ? World->Get(Utf8(Id)) : nullptr;
	return E ? Str(OnlyWorlds::OWJson::Stringify(E->body, /*keep_number_text=*/true)) : FString();
}

bool UOWWorld::SetStringField(const FString& Id, const FString& Key, const FString& Value)
{
	OnlyWorlds::OWElement* E = World ? World->Get(Utf8(Id)) : nullptr;
	if (!E) return false;
	E->Set(Utf8(Key), OnlyWorlds::OWJsonValue::String(Utf8(Value)));
	return true;
}

bool UOWWorld::SetIntField(const FString& Id, const FString& Key, int64 Value)
{
	OnlyWorlds::OWElement* E = World ? World->Get(Utf8(Id)) : nullptr;
	if (!E) return false;
	E->Set(Utf8(Key), OnlyWorlds::OWJsonValue::Number(static_cast<int64_t>(Value)));
	return true;
}

TArray<FString> UOWWorld::GetErrors() const
{
	TArray<FString> Out;
	if (World)
	{
		for (const std::string& Line : World->errors) Out.Add(Str(Line));
	}
	return Out;
}

bool UOWWorld::WriteFolder(const FString& Path, bool bOnlyEdited, FString& Error)
{
	if (!World)
	{
		Error = TEXT("no world");
		return false;
	}
	std::string Why;
	const bool bOk = World->WriteFolder(OWConvert::Path(Path), bOnlyEdited, &Why);
	Error = Str(Why);
	return bOk;
}

bool UOWWorld::PackToFile(const FString& ArchivePath, FString& Error, bool bIncludeAuthorFields)
{
	if (!World)
	{
		Error = TEXT("no world");
		return false;
	}
	OnlyWorlds::OWPacked Packed = World->Pack(Utf8(FPaths::GetBaseFilename(ArchivePath)), false, false, bIncludeAuthorFields);
	if (!Packed.ok)
	{
		Error = Str(Packed.message);
		return false;
	}
	if (!OnlyWorlds::OWFiles::Write(OWConvert::Path(ArchivePath), Packed.bytes))
	{
		Error = TEXT("could not write ") + ArchivePath;
		return false;
	}
	Error.Reset();
	return true;
}
