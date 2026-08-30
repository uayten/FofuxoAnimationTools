// Fofuxo -- o deslocamento do osso, aplicado durante o retarget

#include "FofuxoAnexosOp.h"

#include "Retargeter/IKRetargetProcessor.h"

bool FFofuxoAnexosOp::Initialize(
	const FIKRetargetProcessor&,
	const FRetargetSkeleton&,
	const FTargetSkeleton& InTargetSkeleton,
	const FIKRetargetOpBase*,
	FIKRigLogger&)
{
	OssosComDeslocamento.Reset();
	OssosComDeslocamento.SetNum(Settings.Anexos.Num());

	for (int32 Linha = 0; Linha < Settings.Anexos.Num(); ++Linha)
	{
		const FFofuxoAnexo& Anexo = Settings.Anexos[Linha];

		// O indice sai daqui mesmo com o deslocamento em zero: quem esta com o gizmo
		// na mao move de zero para alguma coisa sem reinicializar nada, e uma linha
		// resolvida so quando ja tem valor nunca sairia do lugar.
		OssosComDeslocamento[Linha] = Anexo.Boneco == EFofuxoBoneco::Fonte
			? INDEX_NONE
			: InTargetSkeleton.FindBoneIndexByName(Anexo.OssoNoAlvo.BoneName);
	}

	bIsInitialized = true;
	return true;
}

void FFofuxoAnexosOp::Run(
	FIKRetargetProcessor& InProcessor,
	const double,
	const TArray<FTransform>&,
	TArray<FTransform>& OutTargetGlobalPose)
{
	const FRetargetSkeleton& Esqueleto = InProcessor.GetSkeleton(ERetargetSourceOrTarget::Target);

	const int32 Quantas = FMath::Min(Settings.Anexos.Num(), OssosComDeslocamento.Num());

	for (int32 Linha = 0; Linha < Quantas; ++Linha)
	{
		const int32 Indice = OssosComDeslocamento[Linha];
		const FVector& Deslocamento = Settings.Anexos[Linha].Deslocamento;

		if (Indice == INDEX_NONE
			|| Deslocamento.IsNearlyZero()
			|| !OutTargetGlobalPose.IsValidIndex(Indice))
		{
			continue;
		}

		FTransform Novo = OutTargetGlobalPose[Indice];

		// No frame do proprio osso, e nao no do mundo: assim o deslocamento acompanha
		// a mao quando ela gira, em vez de escorregar dela.
		Novo.AddToTranslation(Novo.GetRotation().RotateVector(Deslocamento));

		// Pelo esqueleto, e nao escrevendo direto no array: os filhos do osso tem que
		// vir junto, senao a arma anda e a ponta dela fica para tras.
		Esqueleto.SetGlobalTransformAndUpdateChildren(Indice, Novo, OutTargetGlobalPose);
	}
}
