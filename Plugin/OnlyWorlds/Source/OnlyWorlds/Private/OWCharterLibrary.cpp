#include "OWCharterLibrary.h"

#include "OWCharter.h"
#include "OWConvert.h"
#include "OWJson.h"
#include "OWLensDoor.h"
#include "OWStrand.h"
#include "OWWorld.h"
#include "OWWorldObject.h"

#include <optional>
#include <string>
#include <vector>

namespace
{
	namespace CharterCore = OnlyWorlds::OWCharter;
	using OnlyWorlds::OWJsonValue;

	FOWCharterDomain DomainOfCore(const OnlyWorlds::OWCharterDomain& Got)
	{
		FOWCharterDomain Out;
		Out.Id = OWConvert::Str(Got.id);
		Out.Type = OWConvert::Str(Got.type);
		Out.Name = OWConvert::Str(Got.name);
		Out.Sentence = OWConvert::Str(Got.sentence);
		for (const OnlyWorlds::OWCharterHub& H : Got.hubs)
		{
			FOWCharterHub& Hub = Out.Hubs.AddDefaulted_GetRef();
			Hub.Type = OWConvert::Str(H.type);
			Hub.Id = OWConvert::Str(H.id);
		}
		return Out;
	}

	FOWReadingAids AidsOfCore(const OnlyWorlds::OWReadingAids& Got)
	{
		FOWReadingAids Out;
		if (Got.charter)
		{
			Out.bHasCharter = true;
			Out.Charter.Id = OWConvert::Str(Got.charter->id);
			Out.Charter.Type = OWConvert::Str(Got.charter->type);
			Out.Charter.Name = OWConvert::Str(Got.charter->name);
			Out.Charter.RulesJson = OWLensDoor::TextOf(Got.charter->rules);
			Out.Charter.Skill = OWLensDoor::IdsOf(Got.charter->skill);
		}
		if (Got.guide)
		{
			Out.bHasGuide = true;
			Out.Guide.Id = OWConvert::Str(Got.guide->id);
			Out.Guide.Type = OWConvert::Str(Got.guide->type);
			Out.Guide.Name = OWConvert::Str(Got.guide->name);
		}
		for (const OnlyWorlds::OWCharterDomain& D : Got.domains) Out.Domains.Add(DomainOfCore(D));
		if (Got.strand)
		{
			Out.bHasStrand = true;
			Out.StrandJson = OWLensDoor::TextOf(Got.strand->ToJson());
		}
		return Out;
	}

	// The world's reading aids for the reader, or false and the reason.
	bool CharterRead(const UOWWorld* World, const FOWLensReader& Reader, bool bOpen, OnlyWorlds::OWReadingAids& Got, FString& Reason)
	{
		const OnlyWorlds::OWWorld* Native = OWLensDoor::NativeOf(World);
		if (!Native)
		{
			Reason = TEXT("no world");
			return false;
		}
		Got = CharterCore::CharterOf(OWLensDoor::SnapshotOf(*Native, nullptr, bOpen), OWLensDoor::ReaderObjectOf(Reader));
		if (!Got.error.empty())
		{
			Reason = OWConvert::Str(Got.error);
			return false;
		}
		return true;
	}
}

bool UOWCharter::CharterOf(const UOWWorld* World, const FOWLensReader& Reader, bool bOpen, FOWReadingAids& ReadingAids,
	FString& Reason)
{
	ReadingAids = FOWReadingAids();
	Reason.Reset();
	OnlyWorlds::OWReadingAids Got;
	if (!CharterRead(World, Reader, bOpen, Got, Reason)) return false;
	ReadingAids = AidsOfCore(Got);
	return true;
}

bool UOWCharter::DomainsOf(const UOWWorld* World, const FOWLensReader& Reader, bool bOpen, TArray<FOWCharterDomain>& Domains,
	FString& Reason)
{
	Domains.Reset();
	Reason.Reset();
	// DomainsOf is CharterOf's list (the core's DomainsOf reads the same), read once here so a mistake is refused.
	OnlyWorlds::OWReadingAids Got;
	if (!CharterRead(World, Reader, bOpen, Got, Reason)) return false;
	for (const OnlyWorlds::OWCharterDomain& D : Got.domains) Domains.Add(DomainOfCore(D));
	return true;
}

bool UOWCharter::ReadingAidsOf(const FString& AnswerJson, FOWReadingAids& ReadingAids, FString& Reason)
{
	ReadingAids = FOWReadingAids();
	Reason.Reset();
	OWJsonValue Answer;
	bool bAnswer = false;
	if (!OWLensDoor::ReadText(AnswerJson, TEXT("answer: "), Answer, bAnswer, Reason)) return false;
	const std::optional<OnlyWorlds::OWReadingAids> Served = CharterCore::Served(Answer);
	if (!Served)
	{
		Reason = TEXT("no reading aids in the answer");
		return false;
	}
	ReadingAids = AidsOfCore(*Served);
	return true;
}

bool UOWCharter::IsCharterElement(const UOWWorld* World, const FString& ElementId)
{
	return CharterCore::IsCharterElement(OWLensDoor::ServedById(World, ElementId));
}

bool UOWCharter::ReadingAidOf(const UOWWorld* World, const FString& ElementId, FString& Aid)
{
	Aid.Reset();
	const std::optional<std::string> Word = CharterCore::ReadingAidOf(OWLensDoor::ServedById(World, ElementId));
	if (!Word) return false;
	Aid = OWConvert::Str(*Word);
	return true;
}
