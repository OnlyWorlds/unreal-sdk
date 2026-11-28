#include "OWJoinNodes.h"

#include "UObject/WeakObjectPtrTemplates.h"

#include "OWCharterDoor.h"
#include "OWClientShared.h"
#include "OWConvert.h"
#include "OWJson.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>

using OWClientShared::ErrorOf;
using OWConvert::Str;
using OWConvert::Utf8;

namespace OWJoinNodesImpl
{
	FString TextOr(const std::optional<std::string>& Text)
	{
		return Text ? Str(*Text) : FString();
	}

	// "" for none: a graph has no absent string.
	std::optional<std::string> TextOrNone(const FString& Text)
	{
		if (Text.IsEmpty()) return std::nullopt;
		return Utf8(Text);
	}

	// A place in the glimpse's things, or -1 for none (the core's -1, and a whole number no index can be).
	int32 PlaceOf(int64_t Place)
	{
		return Place >= 0 && Place <= std::numeric_limits<int32>::max() ? static_cast<int32>(Place) : -1;
	}

	FOWJoinWorld WorldOf(const std::optional<OnlyWorlds::OWJoinWorld>& W)
	{
		FOWJoinWorld Out;
		if (!W) return Out;
		Out.Id = TextOr(W->id);
		Out.Name = Str(W->name);
		Out.Description = TextOr(W->description);
		return Out;
	}

	FOWJoinPreview PreviewOf(const OnlyWorlds::OWJoinPreview& P, const std::optional<OnlyWorlds::OWResponse>& Response)
	{
		FOWJoinPreview Out;
		Out.Reader = Str(P.reader);
		Out.World = WorldOf(P.world);
		Out.InvitedBy = TextOr(P.invited_by);
		Out.OwnerCharacter = TextOr(P.owner_character);
		Out.Role = Str(P.role);
		Out.bWithCharacter = P.with_character;
		Out.ExpiresAt = Str(P.expires_at);
		Out.Note = TextOr(P.note);
		Out.CharacterName = TextOr(P.character_name);
		if (P.glimpse)
		{
			Out.Glimpse.PickedBy = Str(P.glimpse->picked_by);
			for (const OnlyWorlds::OWJoinGlimpseThing& T : P.glimpse->things)
			{
				FOWJoinGlimpseThing& Thing = Out.Glimpse.Things.AddDefaulted_GetRef();
				Thing.Name = Str(T.name);
				Thing.Type = Str(T.type);
			}
			for (const OnlyWorlds::OWJoinGlimpseLink& L : P.glimpse->links)
			{
				FOWJoinGlimpseLink& Link = Out.Glimpse.Links.AddDefaulted_GetRef();
				Link.From = PlaceOf(L.from);
				Link.To = PlaceOf(L.to);
				Link.Field = Str(L.field);
				Link.Label = Str(L.label);
			}
		}
		Out.bIsTable = P.IsTable();
		Out.MaxUses = P.max_uses.value_or(0);
		Out.UsesLeft = P.uses_left.value_or(0);
		Out.bNameRequired = P.name_required.value_or(false);
		if (P.characters)
		{
			for (const OnlyWorlds::OWJoinPick& C : *P.characters)
			{
				FOWJoinPick& Pick = Out.Characters.AddDefaulted_GetRef();
				Pick.Id = Str(C.id);
				Pick.Name = Str(C.name);
				Pick.ImageUrl = TextOr(C.image_url);
				Pick.bTaken = C.taken;
			}
		}
		// The answer as received, byte for byte: a preview holds no secret.
		if (Response) Out.Json = Str(Response->body);
		return Out;
	}

	FOWJoined JoinedOf(const OnlyWorlds::OWJoined& J)
	{
		FOWJoined Out;
		Out.Kind = Str(J.kind);
		Out.bIsPlayer = J.IsPlayer();
		Out.bIsAgent = J.IsAgent();
		Out.Key = Str(J.key);
		Out.Pin = TextOr(J.pin);
		Out.World = WorldOf(J.world);
		if (J.character)
		{
			Out.Character.Id = Str(J.character->id);
			Out.Character.Name = Str(J.character->name);
		}
		Out.Role = TextOr(J.RoleInWorld());
		if (J.seat)
		{
			Out.Seat.Id = Str(J.seat->id);
			Out.Seat.Role = Str(J.seat->role);
			Out.Seat.AgentName = Str(J.seat->agent_name);
		}
		Out.BaseUrl = TextOr(J.base_url);
		if (J.front_door) Out.FrontDoorJson = Str(OnlyWorlds::OWJson::Stringify(*J.front_door, /*keep_number_text=*/true));
		if (J.reading_aids)
		{
			Out.bHasReadingAids = true;
			Out.ReadingAids = OWCharterDoor::AidsOfCore(*J.reading_aids);
		}
		// env and served are left out: each holds the key again (OWJoinNodes.h).
		return Out;
	}

	FOWJoinedMembership MembershipOf(const OnlyWorlds::OWJoinedMembership& M)
	{
		FOWJoinedMembership Out;
		Out.Id = Str(M.id);
		Out.WorldId = Str(M.world_id);
		Out.WorldName = Str(M.world_name);
		Out.Role = Str(M.role);
		Out.JoinedAt = TextOr(M.joined_at);
		Out.RosterCharacter = TextOr(M.roster_character);
		return Out;
	}

	// The words an error says in place of the body it no longer carries.
	const char* const Withheld = "the answer's body is withheld here, since it may hold the world's key";

	// The error of a call whose answer can hold a key (RedeemJoin's) or a membership (AcceptJoin's), as every node gives
	// it but without the body: RawBody "" and nothing read out of it (an element now, a proposal, the conflicts), and a
	// message that was the body's start (a non-2xx that is no envelope) replaced. The message then says so in one clause.
	// An error with no body (a refusal, no answer) is as ErrorOf gives it.
	FOWApiError WithoutBody(const OnlyWorlds::OWApiError& E)
	{
		if (E.raw_body.empty()) return ErrorOf(E);
		OnlyWorlds::OWApiError Kept = E;
		const OnlyWorlds::OWJsonParseResult Parsed = OnlyWorlds::OWJson::Parse(E.raw_body);
		const bool bMessageIsBody = E.kind == OnlyWorlds::OWApiError::Kind::Api && !(Parsed.ok && Parsed.value.IsObject());
		const std::string Said = bMessageIsBody ? std::string() : E.message;
		Kept.message = Said.empty() ? std::string(Withheld) : Said + " (" + Withheld + ")";
		Kept.raw_body.clear();
		Kept.proposal.reset();
		return ErrorOf(Kept);
	}

	OnlyWorlds::OWJoinConfig WhereOf(std::shared_ptr<OnlyWorlds::OWTransport> Transport, const FString& BaseUrl)
	{
		OnlyWorlds::OWJoinConfig Where;
		Where.transport = std::move(Transport);
		// "" is production's root: the core reads it so (OWJoinConfig::base_url).
		Where.base_url = Utf8(BaseUrl);
		return Where;
	}
}

namespace OWJN = OWJoinNodesImpl;

// -- UOWJoin ------------------------------------------------------------------------------------------------------------

FString UOWJoin::ParseJoinLink(const FString& Text)
{
	return Str(OnlyWorlds::OWJoin::ParseLink(Utf8(Text)));
}

// -- UOWClientJoinPreview -------------------------------------------------------------------------------------------------

UOWClientJoinPreview* UOWClientJoinPreview::PreviewJoin(const FString& Code, const FString& BaseUrl)
{
	UOWClientJoinPreview* Node = NewObject<UOWClientJoinPreview>();
	Node->Settings.BaseUrl = BaseUrl;
	Node->CodeText = Code;
	return Node;
}

void UOWClientJoinPreview::Activate()
{
	std::shared_ptr<OnlyWorlds::OWTransport> T = BeginKeyless();
	if (!T) return;
	TWeakObjectPtr<UOWClientJoinPreview> Self(this);
	OnlyWorlds::OWJoin::Preview(Utf8(CodeText), OWJN::WhereOf(std::move(T), Settings.BaseUrl),
		[Self](OnlyWorlds::OWReply<OnlyWorlds::OWJoinPreview> Reply)
		{
			if (UOWClientJoinPreview* Node = Self.Get()) Node->Answer(std::move(Reply));
		});
}

void UOWClientJoinPreview::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJoinPreview> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(OWJN::PreviewOf(Reply.value, Reply.response), FOWApiError());
	else OnFailure.Broadcast(FOWJoinPreview(), ErrorOf(*Reply.error));
	Release();
}

// -- UOWClientJoinRedeem --------------------------------------------------------------------------------------------------

UOWClientJoinRedeem* UOWClientJoinRedeem::RedeemJoin(const FString& Code, const FString& Name, const FString& AgentName,
	const FString& Character, const FString& BaseUrl)
{
	UOWClientJoinRedeem* Node = NewObject<UOWClientJoinRedeem>();
	Node->Settings.BaseUrl = BaseUrl;
	Node->CodeText = Code;
	Node->NameText = Name;
	Node->AgentText = AgentName;
	Node->CharacterText = Character;
	return Node;
}

void UOWClientJoinRedeem::Activate()
{
	std::shared_ptr<OnlyWorlds::OWTransport> T = BeginKeyless();
	if (!T) return;
	OnlyWorlds::OWRedeemOptions As;
	As.name = OWJN::TextOrNone(NameText);
	As.agent_name = OWJN::TextOrNone(AgentText);
	As.character = OWJN::TextOrNone(CharacterText);
	TWeakObjectPtr<UOWClientJoinRedeem> Self(this);
	OnlyWorlds::OWJoin::Redeem(Utf8(CodeText), As, OWJN::WhereOf(std::move(T), Settings.BaseUrl),
		[Self](OnlyWorlds::OWReply<OnlyWorlds::OWJoined> Reply)
		{
			if (UOWClientJoinRedeem* Node = Self.Get()) Node->Answer(std::move(Reply));
		});
}

void UOWClientJoinRedeem::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJoined> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(OWJN::JoinedOf(Reply.value), FOWApiError());
	else OnFailure.Broadcast(FOWJoined(), OWJN::WithoutBody(*Reply.error));
	Release();
}

// -- UOWClientJoinAccept --------------------------------------------------------------------------------------------------

UOWClientJoinAccept* UOWClientJoinAccept::AcceptJoin(const FOWClientConfig& Config, const FString& Code)
{
	UOWClientJoinAccept* Node = NewObject<UOWClientJoinAccept>();
	Node->Settings = Config;
	Node->CodeText = Code;
	return Node;
}

void UOWClientJoinAccept::Activate()
{
	OnlyWorlds::OWClient* C = Begin();
	if (!C) return;
	TWeakObjectPtr<UOWClientJoinAccept> Self(this);
	C->AcceptJoin(Utf8(CodeText), [Self](OnlyWorlds::OWReply<OnlyWorlds::OWJoinedMembership> Reply)
	{
		if (UOWClientJoinAccept* Node = Self.Get()) Node->Answer(std::move(Reply));
	});
}

void UOWClientJoinAccept::Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJoinedMembership> Reply)
{
	if (!Answering()) return;
	if (Reply.Ok()) OnSuccess.Broadcast(OWJN::MembershipOf(Reply.value), FOWApiError());
	else OnFailure.Broadcast(FOWJoinedMembership(), OWJN::WithoutBody(*Reply.error));
	Release();
}
