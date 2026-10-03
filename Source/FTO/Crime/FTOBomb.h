#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/FTOInteractable.h"
#include "Interaction/FTOTalkable.h"
#include "FTOBomb.generated.h"

class AFTOIncident;
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * The "Evil Masterplan" set piece: a (cartoonishly incompetent) villain's device ticking away in the street. E brings
 * up the wires (the conversation panel): red, blue, green and yellow. Three must be cut in the right order, and the
 * device's own label gives a riddle for each ("the colour of a rubber duck"). A wrong wire takes time off the fuse; cut
 * all three and it's defused (the call's handled); let the fuse run out and it goes off: windows, walls, street
 * furniture and anyone close by, and a big hit of chaos. Server-driven; the fuse and the wires replicate.
 */
UCLASS(NotPlaceable)
class FTO_API AFTOBomb : public AActor, public IFTOInteractable, public IFTOTalkable
{
	GENERATED_BODY()

public:
	AFTOBomb();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: arm it for Incident, Fuse seconds on the clock, the wire order rolled from Seed. */
	void Arm(AFTOIncident* InIncident, float Fuse, int32 InSeed);

	/** Seconds left on the fuse (every machine). */
	float GetTimeLeft() const;
	/** Wires cut in the right order so far (of WiresToCut). */
	int32 GetStage() const { return Stage; }
	bool IsDefused() const { return bDefused; }
	bool HasExploded() const { return bExploded; }
	/** Which option (0-3: red, blue, green, yellow) is the right one to cut next (tests). */
	int32 GetNextWire() const;
	/** Server: the wire Index (0-3) is cut by Officer. */
	void CutWire(int32 Index, AFTOCharacter* Officer);
	/** Server (tests): set the fuse. */
	void SetTimeLeft(float Seconds);

	static constexpr int32 NumWires = 4;
	static constexpr int32 WiresToCut = 3;
	/** Seconds a wrong wire takes off the fuse. */
	UPROPERTY(EditDefaultsOnly, Category="Bomb") float WrongWirePenalty = 25.f;
	/** How far the blast reaches, and how hard it hits walls. */
	UPROPERTY(EditDefaultsOnly, Category="Bomb") float BlastRadius = 1600.f;
	UPROPERTY(EditDefaultsOnly, Category="Bomb") float BlastStrength = 160.f;

	// IFTOInteractable
	virtual bool CanInteract(const AFTOCharacter* Officer) const override;
	virtual FText GetInteractPrompt(const AFTOCharacter* Officer) const override;
	virtual void Interact(AFTOCharacter* Officer) override;
	virtual FVector GetInteractLocation() const override;
	virtual float GetInteractRange() const override { return 260.f; }

	// IFTOTalkable: the wires.
	virtual FText GetTalkTitle() const override;
	virtual void GetTalkOptions(const AFTOCharacter* Officer, TArray<FText>& OutOptions) const override;
	virtual bool TalkChoice(AFTOCharacter* Officer, int32 Index) override;

protected:
	/** The wire to cut at Step (0-3), from the seed (every machine). */
	int32 WireFor(int32 Step) const;
	/** The riddle for the wire to cut now. */
	FString Clue() const;
	/** Server: boom. */
	void Explode();
	UFUNCTION() void OnRep_Wires();

	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Case;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UStaticMeshComponent> Dial;
	UPROPERTY(VisibleAnywhere, Category="Components") TArray<TObjectPtr<UStaticMeshComponent>> Wires;
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UTextRenderComponent> Readout;
	/** What officers focus on (queries only; it blocks nothing). */
	UPROPERTY(VisibleAnywhere, Category="Components") TObjectPtr<UBoxComponent> Focus;

	UPROPERTY(Replicated) TObjectPtr<AFTOIncident> Incident;
	UPROPERTY(Replicated) float FuseEndTime = 0.f;
	UPROPERTY(ReplicatedUsing=OnRep_Wires) int32 Seed = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Wires) uint8 CutMask = 0;
	UPROPERTY(Replicated) uint8 Stage = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Wires) bool bDefused = false;
	UPROPERTY(Replicated) bool bExploded = false;
	/** Every machine: when the last tick beeped. */
	int32 LastBeep = -1;
};
