#include "OWViewsLibrary.h"

#include "OWConvert.h"
#include "OWJson.h"
#include "OWLensDoor.h"
#include "OWViews.h"

#include <string>
#include <vector>

namespace
{
	namespace ViewsCore = OnlyWorlds::OWViews;
	using OWConvert::Str;

	TArray<FString> Strings(const std::vector<std::string>& Xs)
	{
		TArray<FString> Out;
		for (const std::string& X : Xs) Out.Add(Str(X));
		return Out;
	}

	FOWViewRow RowOf(const OnlyWorlds::OWViewRow& R)
	{
		FOWViewRow Out;
		Out.Id = Str(R.id);
		Out.Type = Str(R.kind.type);
		Out.Supertype = Str(R.kind.supertype);
		Out.Profile = Str(R.kind.profile);
		Out.Pair = Str(R.kind.pair);
		Out.From = Str(R.from);
		Out.Sentence = Strings(R.sentence);
		Out.Lead = Strings(R.lead);
		for (const OnlyWorlds::OWViewFold& F : R.fold)
		{
			FOWViewFold Fold;
			Fold.Field = Str(F.field);
			Fold.Label = Str(F.label);
			Out.Fold.Add(Fold);
		}
		Out.Layout = Str(R.layout.name);
		Out.LayoutBy = Str(R.layout.by);
		Out.Banner = Strings(R.banner);
		return Out;
	}

	FOWView ViewOfCore(const OnlyWorlds::OWView& V)
	{
		FOWView Out;
		Out.bHasRow = V.row != nullptr;
		if (V.row) Out.Row = RowOf(*V.row);
		if (V.sentence) Out.Sentence = Str(*V.sentence);
		Out.Banners = Strings(V.banners);
		Out.Order = Strings(V.order);
		return Out;
	}
}

bool UOWViews::ViewOf(const FString& ElementJson, FOWView& View, FString& Reason)
{
	View = FOWView();
	Reason.Reset();
	OnlyWorlds::OWJsonValue Element;
	bool bGiven = false;
	if (!OWLensDoor::ReadText(ElementJson, TEXT("element: "), Element, bGiven, Reason)) return false;
	View = ViewOfCore(ViewsCore::ViewOf(Element));
	return true;
}

FOWView UOWViews::ViewOfElement(const UOWWorld* World, const FString& ElementId)
{
	return ViewOfCore(ViewsCore::ViewOf(OWLensDoor::ServedById(World, ElementId)));
}

TArray<FString> UOWViews::SectionFieldsOf(const FString& Type)
{
	return Strings(ViewsCore::SectionFields(OWConvert::Utf8(Type)));
}

FOWViewsTable UOWViews::GetViewsTable()
{
	FOWViewsTable Out;
	Out.Version = ViewsCore::Version;
	Out.Sha256 = Str(ViewsCore::Sha256);
	for (const OnlyWorlds::OWViewBanner& B : ViewsCore::Banners())
	{
		FOWViewBanner Banner;
		Banner.Mark = Str(B.mark);
		Banner.DefinedBy = Str(B.defined_by);
		Out.Banners.Add(Banner);
	}
	for (const auto& L : ViewsCore::Layouts())
	{
		FOWViewLayoutWords Words;
		Words.Name = Str(L.first);
		Words.Sentence = Str(L.second);
		Out.Layouts.Add(Words);
	}
	for (const OnlyWorlds::OWViewRow& R : ViewsCore::Rows()) Out.Rows.Add(RowOf(R));
	return Out;
}
