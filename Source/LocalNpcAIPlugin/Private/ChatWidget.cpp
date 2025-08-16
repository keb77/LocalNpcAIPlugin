#include "ChatWidget.h"
#include "PlayerAiComponent.h"

void UChatWidget::SendMessage(const FString& MessageText)
{
    APawn* PlayerPawn = GetWorld()->GetFirstPlayerController()->GetPawn();
    if (PlayerPawn)
    {
        UPlayerAiComponent* PlayerAiComponent = PlayerPawn->FindComponentByClass<UPlayerAiComponent>();
        if (PlayerAiComponent)
        {
            PlayerAiComponent->SendText(MessageText);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | ChatWidget] PlayerAiComponent not found. Add PlayerAiComponent to Player Pawn to send chat messages."));
		}
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[LocalAINpc | ChatWidget] No player pawn found to send message."));
	}
}