// Fofuxo -- os anexos de preview, guardados no retargeter

#pragma once

#include "Animation/BoneReference.h"
#include "FofuxoEixoDoMundo.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetSettings.h"

#include "FofuxoAnexosOp.generated.h"

/**
 * Em qual dos dois bonecos do visor um anexo aparece.
 *
 * O ERetargetSourceOrTarget da Engine tem so os dois, e faltava o caso mais
 * comum de todos: a mesma arma nos dois lados, para se comparar como ela fica
 * na fonte e no alvo. Era duas linhas iguais, e agora e uma.
 */
UENUM(BlueprintType)
enum class EFofuxoBoneco : uint8
{
	Fonte,
	Alvo,
	Ambos,
};

/**
 * Um asset pendurado num osso, so para o visor.
 *
 * Nao e o mesmo que o Add Preview Asset do editor de esqueleto: aquele mora na
 * USkeletalMesh e na USkeleton, e por isso ele some quando o rig e reimportado
 * como asset novo. Este mora no retargeter, que e o asset que sobrevive a troca
 * dos dois bonecos.
 */
USTRUCT(BlueprintType)
struct FFofuxoAnexo
{
	GENERATED_BODY()

	/**
	 * Desmarcado, este anexo some do visor e continua guardado.
	 *
	 * Serve para ajustar um de cada vez: com a arma das costas e a da mao no ar ao
	 * mesmo tempo, uma esconde a outra justo quando voce quer ver o encaixe. A
	 * caixinha fica no cabecalho da linha, entao da para apagar e acender sem abrir
	 * nenhuma das duas.
	 */
	UPROPERTY(EditAnywhere, Category = "Anexo")
	bool bMostrar = true;

	/** Em quais bonecos do visor este anexo aparece. */
	UPROPERTY(EditAnywhere, Category = "Anexo")
	EFofuxoBoneco Boneco = EFofuxoBoneco::Ambos;

	/**
	 * O osso da fonte onde ele fica pendurado.
	 *
	 * Sao dois campos, e nao um, porque os dois esqueletos quase nunca chamam o
	 * mesmo osso pelo mesmo nome -- se chamassem, nao haveria retarget a fazer. O
	 * que aparece aqui e a lista de ossos do proprio lado, e cada campo so aparece
	 * quando aquele lado esta em jogo.
	 */
	UPROPERTY(EditAnywhere, Category = "Anexo",
		meta = (EditCondition = "Boneco != EFofuxoBoneco::Alvo", EditConditionHides))
	FBoneReference OssoNaFonte;

	/** O osso do alvo onde ele fica pendurado. */
	UPROPERTY(EditAnywhere, Category = "Anexo",
		meta = (EditCondition = "Boneco != EFofuxoBoneco::Fonte", EditConditionHides))
	FBoneReference OssoNoAlvo;

	/** O que pendurar. */
	UPROPERTY(EditAnywhere, Category = "Anexo",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh,/Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> Asset;


	/**
	 * Move o osso do alvo, e sai nas animacoes exportadas.
	 *
	 * Isto e de outra natureza que o Alinhar no mundo, e vale saber a diferenca. O
	 * alinhamento escreve na *pose de retarget*, que e onde mora a definicao de
	 * neutro -- e por isso o efeito sai em todos os frames de graca. A pose de
	 * retarget, porem, guarda rotacao por osso e um unico deslocamento de raiz:
	 * translacao por osso nao cabe la. Entao esta aqui e um offset somado
	 * *durante* o retarget, depois da pose calculada, que e o que o LocalOffset do
	 * Pin Bone da engine faz.
	 *
	 * Duas consequencias que nao sao bug:
	 *
	 * - O "Reset Selected Bones" limpa a rotacao e nao limpa isto, e o "Copiar
	 *   pose" leva a rotacao para outro RTG e nao leva isto.
	 * - No Editing Retarget Pose isto nao aparece: la o retarget nao roda, e este
	 *   valor so existe enquanto ele roda.
	 *
	 * O deslocamento e dito no frame do proprio osso, entao ele acompanha a mao
	 * quando ela gira, em vez de escorregar dela.
	 *
	 * So o alvo. A fonte e o dado de entrada do retarget -- um op le a pose dela e
	 * nao a escreve, e por isso nao existe deslocamento de fonte que pudesse valer.
	 */
	UPROPERTY(EditAnywhere, Category = "Anexo",
		meta = (DisplayName = "Deslocar o osso -- sai na animacao"))
	FVector Deslocamento = FVector::ZeroVector;

	/**
	 * Mexe so no asset pendurado, e nunca no osso.
	 *
	 * A diferenca para o Deslocamento e o que cada um conserta. O Deslocamento
	 * conserta *o osso*: ele entra no retarget e sai nas animacoes exportadas. Este
	 * aqui conserta *a malha*: ele e o transform relativo do componente de preview,
	 * morre no visor e nao chega em animacao nenhuma.
	 *
	 * Serve para duas coisas, e so para elas:
	 *
	 * 1. Pivo torto. Se a origem do modelo da espada nao esta no cabo, quem esta
	 *    errado e o modelo -- e deslocar o osso para compensar assaria a compensacao
	 *    dentro de toda animacao exportada, escondendo o problema em vez de resolver.
	 * 2. O lado da fonte. Um op le a pose da fonte e escreve a do alvo, entao o
	 *    Deslocamento nao alcanca a fonte. Se a espada do Manny esta no lugar errado
	 *    no visor, este e o unico jeito.
	 *
	 * Fora desses dois casos, o Deslocamento e que e o certo.
	 */
	UPROPERTY(EditAnywhere, Category = "Anexo",
		meta = (DisplayName = "Encaixe do preview -- nao sai na animacao"))
	FTransform Encaixe = FTransform::Identity;

	/**
	 * Para onde a ponta do osso vai apontar quando voce clicar em Alinhar no mundo.
	 *
	 * *Qual* eixo nao muda nada, desde que seja o mesmo no personagem e na arma --
	 * o que importa e a referencia ser externa aos dois, e nao medida em um deles.
	 * E' por isso que o botao alinha os dois lados de uma vez quando o anexo esta
	 * em Ambos: e a mesma constante nos dois, e ai eles batem sem ninguem medir.
	 */
	UPROPERTY(EditAnywhere, Category = "Anexo")
	EFofuxoEixoDoMundo Eixo = EFofuxoEixoDoMundo::MaisX;
};

/** A lista, que e o que aparece no painel de detalhes quando o op e selecionado. */
USTRUCT(BlueprintType, meta = (DisplayName = "Anexos de Preview (Fofuxo)"))
struct FFofuxoAnexosOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Anexos")
	TArray<FFofuxoAnexo> Anexos;

	virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override
	{
		Anexos = static_cast<const FFofuxoAnexosOpSettings*>(InSettingsToCopyFrom)->Anexos;
	}

#if WITH_EDITORONLY_DATA
	/**
	 * De qual esqueleto o seletor de osso tira os nomes.
	 *
	 * A resposta sai so do nome da propriedade, sem olhar para a linha: separar em
	 * dois campos tirou a duvida junto. O campo da fonte lista os ossos do Manny, o
	 * do alvo lista os do personagem, e nenhum dos dois depende de o anexo estar em
	 * Fonte, Alvo ou Ambos.
	 */
	virtual USkeleton* GetSkeleton(const FName InPropertyName) override
	{
		if (InPropertyName == GET_MEMBER_NAME_CHECKED(FFofuxoAnexo, OssoNaFonte))
		{
			return const_cast<USkeleton*>(SourceSkeletonAsset);
		}

		return const_cast<USkeleton*>(TargetSkeletonAsset);
	}
#endif
};

/**
 * Guarda a lista de anexos dentro do retargeter, e aplica o deslocamento de cada
 * um no osso do alvo enquanto o retarget roda.
 *
 * O motivo de ser um op, e nao um asset a parte, e que o UIKRetargeter e da
 * Engine e nao aceita propriedades novas. A pilha de ops e o unico lugar dentro
 * dele que aceita dado de terceiro: FInstancedStruct, salvo no asset, com painel
 * de detalhes proprio e undo/redo de graca.
 *
 * Ele nasceu como carregador de dados, com o Run() vazio. Deixou de ser: o
 * Deslocamento de cada linha e translacao por osso, e translacao nao cabe na
 * pose de retarget -- so pode ser somada durante o retarget, que e aqui.
 *
 * **Este op tem que ficar depois do FK Chains e do Run IK Rig na pilha.** Os ops
 * rodam em ordem, e quem escreve por ultimo manda: com ele em cima, o FK Chains
 * recalcula o osso depois e o deslocamento se perde sem dizer nada.
 *
 * Nao implementa CollectRetargetedBones de proposito. Aquilo declara posse: osso
 * registrado ali para de ser parenteado por outros ops. Este op nao e dono do
 * osso da arma -- o FK Chains e -- ele so empurra o resultado um pouco.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Anexos de Preview (Fofuxo)"))
struct FFofuxoAnexosOp : public FIKRetargetOpBase
{
	GENERATED_BODY()

	FOFUXORETARGETOPS_API virtual bool Initialize(
		const FIKRetargetProcessor& InProcessor,
		const FRetargetSkeleton& InSourceSkeleton,
		const FTargetSkeleton& InTargetSkeleton,
		const FIKRetargetOpBase* InParentOp,
		FIKRigLogger& Log) override;

	FOFUXORETARGETOPS_API virtual void Run(
		FIKRetargetProcessor& InProcessor,
		const double InDeltaTime,
		const TArray<FTransform>& InSourceGlobalPose,
		TArray<FTransform>& OutTargetGlobalPose) override;

	virtual FIKRetargetOpSettingsBase* GetSettings() override { return &Settings; }

	virtual const UScriptStruct* GetSettingsType() const override
	{
		return FFofuxoAnexosOpSettings::StaticStruct();
	}

	virtual const UScriptStruct* GetType() const override
	{
		return FFofuxoAnexosOp::StaticStruct();
	}

	/** Duas listas de anexos no mesmo retargeter seriam duas respostas para a mesma pergunta. */
	virtual bool IsSingleton() const override { return true; }

	UPROPERTY()
	FFofuxoAnexosOpSettings Settings;

private:
	/**
	 * O indice do osso de cada linha que tem deslocamento, resolvido no Initialize.
	 *
	 * Procurar por nome dentro do Run() seria uma busca por linha por quadro, e o
	 * esqueleto nao muda entre uma inicializacao e outra.
	 */
	TArray<int32> OssosComDeslocamento;
};
