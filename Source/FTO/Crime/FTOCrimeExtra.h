#pragma once

#include "CoreMinimal.h"
#include "City/FTOPedestrian.h"
#include "FTOCrimeExtra.generated.h"

class AFTOIncident;

/** Who an extra is in the scene. */
UENUM()
enum class EFTOExtraRole : uint8
{
	Victim,		// being mugged or pickpocketed: hands up (or oblivious), then flags the police down
	Brawler,	// trading blows with the perp; scarpers when the police turn up
	Arguer,		// the other half of a domestic
	Bystander	// one of the crowd a pickpocket's working: idling, on the phone, none the wiser
};

/**
 * Someone else caught up in a crime, so the perp isn't just stood there on their own: the mugger's victim with their
 * hands up, the other bloke in the bar fight, the flatmate in the row about the washing-up. Spawned and tidied away
 * by the incident (server); replicated like the crowd.
 *
 * Talk to a victim (E) for a statement: what the suspect looks like and which way they went.
 */
UCLASS()
class FTO_API AFTOCrimeExtra : public AFTOPedestrian
{
	GENERATED_BODY()

public:
	AFTOCrimeExtra();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: stand at Where facing FaceAt, playing Role in Incident. */
	void SetupExtra(AFTOIncident* InIncident, EFTOExtraRole InRole, const FVector& Where, const FVector& FaceAt, int32 Seed);

	EFTOExtraRole GetRole() const { return ExtraRole; }

	// IFTOInteractable (talking to them: the victim's statement is what they saw)
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual FText GetTalkTitle() const override;

	// IFTOAnimatedActor
	virtual EFTOAnimAction GetAnimAction() const override;

	virtual void Resume() override;

	/** Seconds between the perp knocking a brawler flat (a range). */
	UPROPERTY(EditDefaultsOnly, Category="Extra") FVector2D KnockdownEvery = FVector2D(6.f, 11.f);

protected:
	virtual void OnArrived() override;
	virtual FString AnswerWhatTheySaw() override;
	virtual FString Contraband() override;

	/** On hot dog day the pickpocket's crowd dresses up too (or they'd stand out). */
	virtual bool WantsHotDogSuit() const override { return ExtraRole == EFTOExtraRole::Bystander; }

	/** Is the perp still at it, right here (not caught, not gone)? */
	bool IsCrimeGoingOn() const;
	/** Server: off out of it (a brawler when the police arrive). */
	void Scarper();

	UPROPERTY(Replicated) TObjectPtr<AFTOIncident> Incident;
	UPROPERTY(Replicated) EFTOExtraRole ExtraRole = EFTOExtraRole::Victim;

	FVector Spot = FVector::ZeroVector;
	float SpotYaw = 0.f;
	bool bScarpering = false;
	/** Server: a bystander shuffles about the crowd now and then. */
	float NextShuffle = 0.f;
	float NextKnockdown = 0.f;
	/** Server: when a brawler next swings back at the perp. */
	float NextSwing = 0.f;
};
