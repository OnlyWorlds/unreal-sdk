#pragma once
// Images and files on Blueprints: one async node per answer's shape, each with OnSuccess and OnFailure, over the core's
// OWClient image and file calls (OWClient.h; their types in OWMedia.h). As every client node (OWClientNodes.h), each
// builds its own client from an FOWClientConfig and sends through FOWHttpTransport, so it answers later, on the game
// thread; a refusal answers at once, inside the node's activation.
//
// An upload takes its bytes as an Array of Byte, or a path: the engine's own image nodes write to disk by path
// (Export Render Target, Export Texture2D, Export to Disk), and none reads a file into bytes, so the FromDisk factories
// read the file themselves, on the game thread; a file they cannot read is refused, nothing sent. The ticket calls
// (CreateMediaTicket, CreateRemovalTicket, CreateFileTicket) are in the core only: a ticket is for sending the bytes
// yourself, which a graph does not do.
//
// The whole of an upload is one request under the transport's timeout, 30 seconds by default: a large file on a slow
// connection can run past it and fail as a Transport error. A C++ caller passes its own (SetTransport with
// FOWHttpTransport::Make(Seconds)); the nodes have no pin for it.
//
// A text a graph cannot leave absent (Key, Label) is "" for none. Never keep a key or a PIN in an asset.

#include "CoreMinimal.h"

#include "OWClientNodes.h"

#include "OWFileNodes.generated.h"

/** The upload host's answer for one image. Put Url in an element's image_url (PatchElement). The core's OWUploadedImage. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWUploadedImage
{
	GENERATED_BODY()

	/** The permanent public URL (https://media.onlyworlds.com/<key>). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Url;

	/** The object key, under the ticket's prefix. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	int64 Bytes = 0;

	/** The MIME type the host read from the bytes. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Etag;
};

/** The upload host's answer for one image removed. The core's OWRemovedImage. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWRemovedImage
{
	GENERATED_BODY()

	/** The object key removed. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Removed;

	/** The bytes given back to the uploading account's storage. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	int64 Bytes = 0;

	/** may_linger: copies the network or a browser already holds can keep answering for a while; purging: the cache is cleared too. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Cache;
};

/** One file on an element. On the world's list it names its element (ElementType, ElementId); on an element's, those are "". The core's OWFileRow and OWWorldFileRow. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWFileRow
{
	GENERATED_BODY()

	/** The file's id: what ReadFileDownloadLink and RemoveElementFile take. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Id;

	/** The file's name as uploaded. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Name;

	/** pdf, txt, md, json, csv, yaml, toml, zip, png, jpg, webp or avif. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Kind;

	/** Bytes, as the host stored them. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	int64 Size = 0;

	/** A field of the element's type the file belongs to, or free text; "" for none. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Label;

	/** Who added it: a member's or a seat's name, the owner's username, or "a former member". */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString AddedByName;

	/** Their membership id; "" for the owner's files and for a membership no longer there. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString AddedByMembership;

	/** An agent added it: an AI seat, the MCP surface, or a key that declared an agent. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	bool bAddedByAgent = false;

	/** The world's current owner added it, as the API serves it. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	bool bAddedByOwner = false;

	/** When the host stored it (ISO 8601). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString AddedAt;

	/** On the world's list, the element's type; "" on an element's. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString ElementType;

	/** On the world's list, the element's id; "" on an element's. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString ElementId;
};

/** The upload host's answer for one file. The core's OWUploadedFile. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWUploadedFile
{
	GENERATED_BODY()

	/** The object key the file landed under. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Key;

	/** The file's id, the Id its row will have. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Fid;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	int64 Bytes = 0;

	/** The MIME type the file is served with. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Etag;
};

/** A link to a file, and when it stops working. Url is the credential: anyone holding it can fetch the file until Exp. Use it at once; never save, print or share it. The core's OWFileDownload. */
USTRUCT(BlueprintType)
struct ONLYWORLDS_API FOWFileDownload
{
	GENERATED_BODY()

	/** The file on the download host, as an attachment. */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Url;

	/** Unix seconds after which the link is refused (5 minutes after issue). */
	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	int64 Exp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	FString Kind;

	UPROPERTY(BlueprintReadOnly, Category = "OnlyWorlds|Files")
	int64 Size = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWImageAnswer, const FOWUploadedImage&, Image, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWImageRemovalAnswer, const FOWRemovedImage&, Removal, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOWFilesAnswer, const TArray<FOWFileRow>&, Files, int64, UsedBytes, int64, CapBytes, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWFileUploadAnswer, const FOWUploadedFile&, File, const FOWApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOWFileLinkAnswer, const FOWFileDownload&, Link, const FOWApiError&, Error);

/**
 * Upload one image and get its permanent public URL: a ticket from the API (a write: the key and its PIN), then the
 * bytes straight to the upload host with the ticket alone. webp, png, jpeg or avif, read from the bytes (never SVG or
 * gif). Set the answer's Url as an element's image_url yourself. Error.Code is the host's when it refuses: too_large
 * (also before any byte is sent, past the ticket's limit), ticket_expired, exists, unsupported_type; the API's on the
 * ticket: storage_full, quota_exceeded.
 */
UCLASS()
class ONLYWORLDS_API UOWClientImage : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The host's answer (Image), or why not (Error). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWImageAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWImageAnswer OnFailure;

	/** The image's bytes to the world. Key, the object key sent as X-Key (it must start with the ticket's prefix): "" lets the host pick one. The core's UploadImage. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientImage* UploadImage(const FOWClientConfig& Config, const TArray<uint8>& Image, const FString& Key);

	/** The image file at Path (a screenshot or a render a graph saved) to the world, read here; a file that cannot be read is refused, nothing sent. Key as on UploadImage. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientImage* UploadImageFromDisk(const FOWClientConfig& Config, const FString& Path, const FString& Key);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWUploadedImage> Reply);

	TArray<uint8> Bytes;
	FString FromPath;
	bool bFromDisk = false;
	FString KeyText;
};

/**
 * Remove one uploaded image: a removal ticket from the API (the image's uploader or the world's owner key, with its
 * PIN), then the removal at the upload host. Elements still showing it keep their image_url: clear it yourself.
 */
UCLASS()
class ONLYWORLDS_API UOWClientImageRemoval : public UOWClientNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWImageRemovalAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWImageRemovalAnswer OnFailure;

	/** KeyOrUrl: the object key, or the whole image_url. Error.Code permission_error (neither uploader nor owner), not_found, upload_pending (wait Error.RetryAfter), not_removable. The core's RemoveImage. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientImageRemoval* RemoveImage(const FOWClientConfig& Config, const FString& KeyOrUrl);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWRemovedImage> Reply);

	FString KeyText;
};

/** The files on one element, or in the world: newest first, with the key alone (a read, no PIN). Files never ride on an element: not in its body or the change feed. */
UCLASS()
class ONLYWORLDS_API UOWClientFiles : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** The rows (Files), and on the world's list its UsedBytes of CapBytes (0 on an element's), or why not (Error). */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWFilesAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWFilesAnswer OnFailure;

	/** GET /{type}/{id}/files: the element's files. An element this key cannot see is Error.Code not_found. The core's ListFiles. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFiles* ListElementFiles(const FOWClientConfig& Config, const FString& Type, const FString& Id);

	/** GET /world/files: every file this key may see, each naming its element, and the world's total (UsedBytes counts every file, also those this key cannot see). The core's WorldFiles. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFiles* ListWorldFiles(const FOWClientConfig& Config);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<std::vector<OnlyWorlds::OWFileRow>> Reply);
	void AnswerWorld(OnlyWorlds::OWReply<OnlyWorlds::OWWorldFiles> Reply);

	bool bWorld = false;
	FString ElementType;
	FString ElementId;
};

/**
 * Attach one file to an element: a ticket from the API (a write key that may change the element, with its PIN; the
 * kind read from the name's extension), then the bytes straight to the upload host with the ticket alone. The file is
 * listed once the host has reported it, which can be a moment after. Error.Code on the ticket: file_too_large (over
 * 25 MB), files_full (the world's 250 MB), not_author, quota_exceeded; at the host: too_large, exists,
 * unsupported_type (the bytes are not the kind).
 */
UCLASS()
class ONLYWORLDS_API UOWClientFileUpload : public UOWClientNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWFileUploadAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWFileUploadAnswer OnFailure;

	/** File's bytes onto the element. Name is the file's name with its extension (.pdf .txt .md .json .csv .yaml .toml .zip .png .jpg .webp .avif); one keel takes no file with is refused, nothing sent. Label: a field of the element's type the file belongs to, or your own words (at most 80); "" for none. The core's UploadFile. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFileUpload* UploadElementFile(const FOWClientConfig& Config, const FString& Type, const FString& Id,
		const TArray<uint8>& File, const FString& Name, const FString& Label);

	/** The file at Path onto the element, read here, its name the path's file name; a file that cannot be read is refused, nothing sent. Label as on UploadElementFile. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFileUpload* UploadElementFileFromDisk(const FOWClientConfig& Config, const FString& Type, const FString& Id,
		const FString& Path, const FString& Label);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWUploadedFile> Reply);

	FString ElementType;
	FString ElementId;
	TArray<uint8> Bytes;
	FString FromPath;
	bool bFromDisk = false;
	FString NameText;
	FString LabelText;
};

/** A link to one file and when it stops working (5 minutes): the key alone. The link is the credential; ask for it when the file is opened and use it at once. */
UCLASS()
class ONLYWORLDS_API UOWClientFileLink : public UOWClientNode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWFileLinkAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWFileLinkAnswer OnFailure;

	/** GET /{type}/{id}/files/{fileId}/download. FileId is a row's Id. The core's FileDownloadLink. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFileLink* ReadFileDownloadLink(const FOWClientConfig& Config, const FString& Type, const FString& Id,
		const FString& FileId);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWFileDownload> Reply);

	FString ElementType;
	FString ElementId;
	FString FileText;
};

/** Remove one file for real: out of the lists and the world's total at once. With the PIN; the world's owner key or the key of whoever added it. */
UCLASS()
class ONLYWORLDS_API UOWClientFileRemoval : public UOWClientNode
{
	GENERATED_BODY()

public:
	/** Json is always "": nothing comes back but the success. */
	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWJsonAnswer OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "OnlyWorlds|Files")
	FOWJsonAnswer OnFailure;

	/** DELETE /{type}/{id}/files/{fileId}. Error.Code permission_error (neither owner nor uploader), not_found. The core's RemoveFile. */
	UFUNCTION(BlueprintCallable, Category = "OnlyWorlds|Files", meta = (BlueprintInternalUseOnly = "true"))
	static UOWClientFileRemoval* RemoveElementFile(const FOWClientConfig& Config, const FString& Type, const FString& Id,
		const FString& FileId);

	virtual void Activate() override;

private:
	void Answer(OnlyWorlds::OWReply<OnlyWorlds::OWJsonValue> Reply);

	FString ElementType;
	FString ElementId;
	FString FileText;
};
