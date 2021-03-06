// Fill out your copyright notice in the Description page of Project Settings.


#include "MenuWidget.h"
#include <string>

bool UMenuWidget::SaveUniqueId(FString id) {
	if (UMySaveGame* SaveGameInstance = Cast<UMySaveGame>(UGameplayStatics::CreateSaveGameObject(UMySaveGame::StaticClass())))
	{
		// Set data on the savegame object.
		SaveGameInstance->UniqueId = id;

		// Save the data immediately.
		if (UGameplayStatics::SaveGameToSlot(SaveGameInstance, "MirageSDK", 0))
		{
			return true;
			// Save succeeded.
		}
	}
	return false;
}

FString UMenuWidget::LoadUniqueId() {
	if (UMySaveGame* LoadedGame = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromSlot("MirageSDK", 0)))
	{
		// The operation was successful, so LoadedGame now contains the data we saved earlier.
		return LoadedGame->UniqueId;
	}
	return "";
}

UMenuWidget::UMenuWidget(const FObjectInitializer& ObjectInitializer) : UUserWidget(ObjectInitializer) {
	FString uniqueId = this->LoadUniqueId();
	if (uniqueId == "") {
		uniqueId = FGuid::NewGuid().ToString();
		SaveUniqueId(uniqueId);
	}
	this->deviceId = uniqueId;
}


bool UMenuWidget::GetClient(FMirageConnectionStatus Status)
{
	Http = &FHttpModule::Get();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindLambda([Status, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

			// Deserialize the json data given Reader and the actual object to deserialize
			if (FJsonSerializer::Deserialize(Reader, JsonObject))
			{
				FString recievedUri = JsonObject->GetStringField("uri");
				FString sessionId = JsonObject->GetStringField("session");
				bool needLogin = JsonObject->GetBoolField("login");

				// Output it for debug
				GEngine->AddOnScreenDebugMessage(1, 2.0f, FColor::Green, recievedUri);

				// Set session from backend clientId for future calls
				this->clientId = sessionId;

				// Open Metamask
				if (needLogin) {
					FPlatformProcess::LaunchURL(recievedUri.GetCharArray().GetData(), NULL, NULL);
				}
				Status.Execute(true);
			}
			else {
				Status.Execute(false);
			}
			
		});

	FString url = baseUrl + "connect";
	GEngine->AddOnScreenDebugMessage(1, 2.0f, FColor::Green, url);

	Request->SetURL(url);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), "X-MirageSDK-Agent");
	Request->SetHeader("Content-Type", TEXT("application/json"));
	Request->SetContentAsString("{\"device_id\": \"" + deviceId + "\"}");
	Request->ProcessRequest();
	return true;
}

void UMenuWidget::SendTransaction(FString contract, FString abi, FString method, FString args, FMirageTicket Ticket)
{
	Http = &FHttpModule::Get();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindLambda([Ticket, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

			if (FJsonSerializer::Deserialize(Reader, JsonObject))
			{
				FString ticketId = JsonObject->GetStringField("ticket");
				GEngine->AddOnScreenDebugMessage(1, 2.0f, FColor::Green, ticketId);

				Ticket.Execute(ticketId);
			}
		});

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Request, contract, abi, method, args]() {
		FString url = this->baseUrl + "send/transaction";
		Request->SetURL(url);
		Request->SetVerb("POST");
		Request->SetHeader(TEXT("User-Agent"), "X-MirageSDK-Agent");
		Request->SetHeader("Content-Type", TEXT("application/json"));
		Request->SetContentAsString("{\"device_id\": \"" + this->deviceId + "\", \"contract_address\": \"" + contract + "\", \"abi_hash\": \"" + abi + "\", \"method\": \"" + method + "\", \"args\": \"" + args + "\"}"); // erc20 abi
		Request->ProcessRequest();
	});

	// Open Metamask
	FPlatformProcess::LaunchURL(this->clientId.GetCharArray().GetData(), NULL, NULL);
}

void UMenuWidget::GetTicketResult(FString ticketId, FMirageTicketResult Result)
{
	int i = 0;
	int code = 0;

	while (i < 10 && code == 0)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
		Request->OnProcessRequestComplete().BindLambda([Result, ticketId, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
			{
				TSharedPtr<FJsonObject> JsonObject;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

				if (FJsonSerializer::Deserialize(Reader, JsonObject))
				{
					int code = JsonObject->GetIntegerField("code");
					FString status = JsonObject->GetStringField("status");
					Result.Execute("Transaction status: " + status, code);
				}
			});

		FString url = baseUrl + "result";
		Request->SetURL(url);
		Request->SetVerb("POST");
		Request->SetHeader(TEXT("User-Agent"), "X-MirageSDK-Agent");
		Request->SetHeader("Content-Type", TEXT("application/json"));
		// Send clientId to backend to redirect metamask 
		Request->SetContentAsString("{\"device_id\": \"" + deviceId + "\", \"ticket\": \"" + ticketId + "\" }"); // erc20 abi
		Request->ProcessRequest();
		FPlatformProcess::Sleep(10000);
		i++;
	}
}

void UMenuWidget::GetData(FString contract, FString abi, FString method, FString args, FMirageDelegate Result)
{
	Http = &FHttpModule::Get();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindLambda([Result, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			Result.Execute(Response->GetContentAsString());
		});

	FString url = baseUrl + "call/method";
	Request->SetURL(url);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), "X-MirageSDK-Agent");
	Request->SetHeader("Content-Type", TEXT("application/json"));
	// Send clientId to backend to redirect metamask 
	Request->SetContentAsString("{\"device_id\": \"" + deviceId + "\", \"contract_address\": \"" + contract + "\", \"abi_hash\": \"" + abi + "\", \"method\": \"" + method + "\", \"args\": \"" + args + "\"}"); // erc20 abi
	Request->ProcessRequest();
}

void UMenuWidget::SendABI(FString abi, FMirageDelegate Result)
{
	Http = &FHttpModule::Get();

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindLambda([Result, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			Result.Execute(JsonObject->GetStringField("abi_hash"));
		});

	FString url = baseUrl + "call/method";
	Request->SetURL(url);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), "X-MirageSDK-Agent");
	Request->SetHeader("Content-Type", TEXT("application/json"));
	// Send clientId to backend to redirect metamask
	
	const TCHAR* find = TEXT("\"");
		const TCHAR* replace = TEXT("\\\"");
	Request->SetContentAsString("{\"abi\": \"" + abi.Replace(find, replace, ESearchCase::IgnoreCase) + "\"}"); // erc20 abi
	Request->ProcessRequest();
}
