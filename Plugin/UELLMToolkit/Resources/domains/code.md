# Code Domain — C++ Gameplay Patterns

## UE 5.7 Modern Practices

### TObjectPtr (Required in Headers)
```cpp
// Headers — always TObjectPtr
UPROPERTY(EditAnywhere)
TObjectPtr<UStaticMeshComponent> MeshComp;

// .cpp files — raw pointers are fine for local variables
UStaticMeshComponent* Comp = GetComponentByClass<UStaticMeshComponent>();
```

### UPROPERTY Gotchas
- `EditDefaultsOnly` for tuning values (only on CDO, not per-instance)
- `BlueprintReadOnly` for state consumed by AnimBP/UI
- `Transient` for runtime-only state that shouldn't serialize
- `ReplicatedUsing=OnRep_X` requires `DOREPLIFETIME` in `GetLifetimeReplicatedProps`
- Missing `UPROPERTY()` on a `TObjectPtr` = GC collects it. Instant crash on next access.

### UFUNCTION Patterns
```cpp
// Blueprint-callable with execution pin
UFUNCTION(BlueprintCallable, Category = "Combat")
void StartAttack();

// Pure — no exec pin, no side effects
UFUNCTION(BlueprintPure, Category = "Stats")
float GetHealthPercent() const;

// Native event — C++ default, BP can override
UFUNCTION(BlueprintNativeEvent, Category = "Events")
void OnHitReact(float Damage);
// Implementation goes in OnHitReact_Implementation()
```

### Forward Declarations
Always forward-declare in headers, `#include` in .cpp. Reduces compile times and avoids circular dependencies.
```cpp
// Header
class UAnimMontage;  // Forward declare
class UInputAction;

// .cpp
#include "Animation/AnimMontage.h"
#include "InputAction.h"
```

## Character Movement Component

### Subclassing CMC
```cpp
UCLASS()
class MYPROJECT_API UMyCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    // Override for mode-aware behavior
    virtual float GetMaxAcceleration() const override;
    virtual float GetMaxSpeed() const override;

    UPROPERTY(EditDefaultsOnly, Category = "Movement")
    float StrafeMaxAcceleration = 800.f;
};
```

### Accel vs Speed
- `MaxAcceleration` controls how quickly you reach max speed (responsiveness)
- `MaxWalkSpeed` controls the cap (feel)
- For strafe/combat: lower accel = floatier, higher = snappier
- Override `GetMaxAcceleration()` to return different values per movement mode

### Registering Custom CMC
```cpp
AMyCharacter::AMyCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UMyCharacterMovement>(
        ACharacter::CharacterMovementComponentName))
{
}
```

## Component Architecture

### CreateDefaultSubobject (Constructor Only)
```cpp
AMyActor::AMyActor()
{
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    MeshComp->SetupAttachment(SceneRoot);
}
```

### Runtime Component Creation
```cpp
void AMyActor::BeginPlay()
{
    Super::BeginPlay();
    auto* Comp = NewObject<UBoxComponent>(this, TEXT("DynamicBox"));
    Comp->SetupAttachment(RootComponent);
    Comp->RegisterComponent();
}
```

### Attachment Rules
- `SetupAttachment()` — construction only, before `RegisterComponent`
- `AttachToComponent()` — runtime, with `FAttachmentTransformRules`
- `KeepRelativeTransform` (default) vs `SnapToTargetNotIncludingScale` vs `KeepWorldTransform`

## Build Strategy

### Live Coding Safe Changes
- Function body edits (no signature changes)
- Default value changes in constructors
- Adding/removing log statements

### Full Rebuild Required
- Adding/removing `UCLASS`, `USTRUCT`, `UENUM`
- Changing `UPROPERTY`/`UFUNCTION` specifiers
- Adding/removing header files
- Changing `.Build.cs` dependencies
- Modifying struct layouts (adding/removing/reordering members)

### Struct Layout Crash
Changing a `USTRUCT` layout and using live coding = crash. Always do a full rebuild after struct changes. The editor will silently use stale reflection data and corrupt memory.

## Common UE 5.7 Pitfalls

- **`SetFrameRate()` crashes** on animation assets with incompatible rate changes. Use FBX reimport with `custom_sample_rate` instead.
- **`SavePackage` returns `bool`** in 5.7 (not `FSavePackageResultStruct` like older versions).
- **Interchange is the default import pipeline** — `UInterchangeAssetImportData` not `UFbxAssetImportData`. Transform overrides live on `UInterchangeGenericAssetsPipeline`.
- **`get_asset_by_object_path` deprecated** — use `IAssetRegistry::Get().GetAssetsByPath()` + filter.
- **Win32 `TRUE`/`FALSE` macros unavailable** after UE includes `HideWindowsPlatformTypes.h`. Use `1`/`0` for Win32 BOOL params.

## Module Dependencies

Standard deps: `Core`, `CoreUObject`, `Engine`, `InputCore`. Adding new modules to `.Build.cs` requires a full rebuild (project file regeneration).
