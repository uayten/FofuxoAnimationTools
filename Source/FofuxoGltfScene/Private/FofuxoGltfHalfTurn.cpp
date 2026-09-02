// Fofuxo -- the half turn that matches glTF to FBX

#include "FofuxoGltfHalfTurn.h"

#include "Builders/GLTFMemoryArchive.h"
#include "Json/GLTFJsonAccessor.h"
#include "Json/GLTFJsonAnimation.h"
#include "Json/GLTFJsonBufferView.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonMesh.h"
#include "Json/GLTFJsonNode.h"
#include "Json/GLTFJsonRoot.h"
#include "Json/GLTFJsonSkin.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "FofuxoGltfScene"

namespace FofuxoHalfTurn
{
	/**
	 * The half turn about the up axis, already in glTF's terms: X and Z change
	 * sign, Y stays. The fourth sign is the quaternion's W and the matrix's
	 * homogeneous row -- neither of them turns.
	 */
	static constexpr float Sign[4] = { -1.f, 1.f, -1.f, 1.f };

	enum class ETurnAs
	{
		/**
		 * Position, normal, tangent and quaternion, all in the same bag: turning
		 * is negating components 0 and 2.
		 *
		 * For the quaternion that is the result of conjugating by the half turn,
		 * and not a guess -- the rotation axis turns along and the angle stays,
		 * because a half turn is a real rotation and not a mirror. The tangent
		 * keeps the binormal's sign in W, and W stays for the same reason: a
		 * rotation doesn't swap the system's handedness.
		 */
		Vector,

		/**
		 * The inverse bind matrix. Negating is not enough here: the matrix
		 * changes basis along with everything else, which is M -> D M D, with
		 * D = diag(-1, 1, -1, 1). It comes out as each element multiplied by its
		 * row's sign times its column's.
		 */
		Matrix,
	};

	/** A piece of buffer already checked, ready to be rewritten. */
	struct FTarget
	{
		FGLTFJsonAccessor* Accessor = nullptr;
		uint8* Data = nullptr;
		ETurnAs As = ETurnAs::Vector;
	};

	static int32 ComponentsOf(EGLTFJsonAccessorType Type)
	{
		switch (Type)
		{
		case EGLTFJsonAccessorType::Scalar: return 1;
		case EGLTFJsonAccessorType::Vec2:   return 2;
		case EGLTFJsonAccessorType::Vec3:   return 3;
		case EGLTFJsonAccessorType::Vec4:   return 4;
		case EGLTFJsonAccessorType::Mat2:   return 4;
		case EGLTFJsonAccessorType::Mat3:   return 9;
		case EGLTFJsonAccessorType::Mat4:   return 16;
		default:                            return 0;
		}
	}

	static int32 SizeOf(EGLTFJsonComponentType Type)
	{
		switch (Type)
		{
		case EGLTFJsonComponentType::Int8:
		case EGLTFJsonComponentType::UInt8:  return 1;
		case EGLTFJsonComponentType::Int16:
		case EGLTFJsonComponentType::UInt16: return 2;
		case EGLTFJsonComponentType::Int32:
		case EGLTFJsonComponentType::UInt32:
		case EGLTFJsonComponentType::Float:  return 4;
		default:                             return 0;
		}
	}

	template <typename T>
	static T Negated(T Value)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			return -Value;
		}
		else
		{
			// In a normalized integer the smallest value has no opposite -- in an
			// int8, -128 would become 128, which doesn't fit. The specification
			// already reads -128 and -127 as the same -1, so clamping to the
			// largest loses nothing.
			return Value == TNumericLimits<T>::Min()
				? TNumericLimits<T>::Max()
				: static_cast<T>(-Value);
		}
	}

	template <typename T>
	static void NegateXAndZ(uint8* Data, int32 Count, int32 Components)
	{
		T* Values = reinterpret_cast<T*>(Data);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			T* Element = Values + (int64)Index * Components;

			Element[0] = Negated(Element[0]);
			if (Components > 2)
			{
				Element[2] = Negated(Element[2]);
			}
		}
	}

	static void ConjugateMatrices(uint8* Data, int32 Count)
	{
		float* Values = reinterpret_cast<float*>(Data);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			// glTF stores a matrix by column: the element at row R, column C, is
			// at C * 4 + R.
			float* Matrix = Values + (int64)Index * 16;

			for (int32 Column = 0; Column < 4; ++Column)
			{
				for (int32 Row = 0; Row < 4; ++Row)
				{
					Matrix[Column * 4 + Row] *= Sign[Row] * Sign[Column];
				}
			}
		}
	}
}

bool FFofuxoGltfBuilder::ApplyHalfTurn(FText& OutError)
{
	using namespace FofuxoHalfTurn;

	// The engine's GetRoot hands back const because an outside caller should
	// only read. Here we are writing our own file, one step before serializing
	// -- it is the same object the converters have just filled.
	FGLTFJsonRoot& Root = const_cast<FGLTFJsonRoot&>(GetRoot());

	// Who turns what. An accessor can show up twice -- the same positions
	// serving two mesh primitives, say -- and turning twice would undo the turn,
	// so the map guarantees one pass per accessor.
	TMap<FGLTFJsonAccessor*, ETurnAs> Wanted;

	auto Want = [&Wanted](FGLTFJsonAccessor* Accessor, ETurnAs As)
	{
		if (Accessor != nullptr)
		{
			Wanted.FindOrAdd(Accessor, As);
		}
	};

	for (FGLTFJsonMesh* Mesh : Root.Meshes)
	{
		for (FGLTFJsonPrimitive& Piece : Mesh->Primitives)
		{
			Want(Piece.Attributes.Position, ETurnAs::Vector);
			Want(Piece.Attributes.Normal, ETurnAs::Vector);
			Want(Piece.Attributes.Tangent, ETurnAs::Vector);

			// Morph targets are position and normal deltas: they turn as
			// vectors, for the same reason as what they add to.
			for (FGLTFJsonTarget& Target : Piece.Targets)
			{
				Want(Target.Position, ETurnAs::Vector);
				Want(Target.Normal, ETurnAs::Vector);
			}
		}
	}

	for (FGLTFJsonSkin* Skin : Root.Skins)
	{
		Want(Skin->InverseBindMatrices, ETurnAs::Matrix);
	}

	for (FGLTFJsonAnimation* Animation : Root.Animations)
	{
		for (const FGLTFJsonAnimationChannel& Channel : Animation->Channels)
		{
			if (Channel.Sampler == nullptr)
			{
				continue;
			}

			// The scale track doesn't turn: scale is size per axis, and the half
			// turn only swaps two axes it leaves alike.
			if (Channel.Target.Path == EGLTFJsonTargetPath::Translation
				|| Channel.Target.Path == EGLTFJsonTargetPath::Rotation)
			{
				Want(Channel.Sampler->Output, ETurnAs::Vector);
			}
		}
	}

	// Two passes: first check that everything can be reached, and only then
	// write. Turning half the file would come out with no error at all and with
	// the body folded -- which is exactly the silent failure this pass exists to
	// end.
	const FGLTFMemoryArchive* Buffer = GetBufferData();
	TArray<FTarget> Targets;
	Targets.Reserve(Wanted.Num());

	for (const TPair<FGLTFJsonAccessor*, ETurnAs>& Pair : Wanted)
	{
		FGLTFJsonAccessor* Accessor = Pair.Key;

		const int32 Components = ComponentsOf(Accessor->Type);
		const int32 Size = SizeOf(Accessor->ComponentType);

		const bool bFloatMatrix = Pair.Value == ETurnAs::Matrix
			&& Accessor->Type == EGLTFJsonAccessorType::Mat4
			&& Accessor->ComponentType == EGLTFJsonComponentType::Float;

		const bool bKnownVector = Pair.Value == ETurnAs::Vector
			&& Components >= 3
			&& (Accessor->ComponentType == EGLTFJsonComponentType::Float
				|| Accessor->ComponentType == EGLTFJsonComponentType::Int8
				|| Accessor->ComponentType == EGLTFJsonComponentType::Int16);

		// A ByteStride other than zero would mean data interleaved with another
		// attribute, and the pass below, which walks element by element, would
		// be wrong. The engine's exporter writes each accessor in its own tight
		// block, but checking is cheap.
		const bool bReachable = Buffer != nullptr
			&& Accessor->BufferView != nullptr
			&& Accessor->BufferView->ByteStride == 0
			&& Components > 0
			&& Size > 0;

		if (!bReachable || !(bFloatMatrix || bKnownVector))
		{
			OutError = FText::Format(
				LOCTEXT("GltfTurnUnreachable",
					"I could not turn the data {0} into the FBX's convention. Pick the Blender target, "
					"or export as FBX."),
				FText::FromString(Accessor->Name.IsEmpty() ? FString::FromInt(Accessor->Index) : Accessor->Name));
			return false;
		}

		const int64 Start = Accessor->BufferView->ByteOffset + Accessor->ByteOffset;
		const int64 Bytes = (int64)Accessor->Count * Components * Size;

		if (Start < 0 || Bytes <= 0 || Start + Bytes > Buffer->Num())
		{
			OutError = LOCTEXT("GltfTurnOutOfBuffer",
				"The engine's glTF exporter wrote a piece outside the buffer. Pick the Blender target, "
				"or export as FBX.");
			return false;
		}

		FTarget Target;
		Target.Accessor = Accessor;
		Target.As = Pair.Value;
		Target.Data = const_cast<uint8*>(Buffer->GetData()) + Start;
		Targets.Add(Target);
	}

	// From here down there is no way left to fail.

	for (const FTarget& Target : Targets)
	{
		FGLTFJsonAccessor& Accessor = *Target.Accessor;
		const int32 Components = ComponentsOf(Accessor.Type);

		if (Target.As == ETurnAs::Matrix)
		{
			ConjugateMatrices(Target.Data, Accessor.Count);
			continue;
		}

		switch (Accessor.ComponentType)
		{
		case EGLTFJsonComponentType::Float:
			NegateXAndZ<float>(Target.Data, Accessor.Count, Components);
			break;
		case EGLTFJsonComponentType::Int8:
			NegateXAndZ<int8>(Target.Data, Accessor.Count, Components);
			break;
		case EGLTFJsonComponentType::Int16:
			NegateXAndZ<int16>(Target.Data, Accessor.Count, Components);
			break;
		default:
			checkNoEntry();
			break;
		}

		// The position accessor carries the smallest and the largest of each
		// axis, and the reader trusts them. Negating an axis swaps its ends.
		if (Accessor.MinMaxLength >= 3)
		{
			const float SmallestX = Accessor.Min[0];
			const float SmallestZ = Accessor.Min[2];

			Accessor.Min[0] = -Accessor.Max[0];
			Accessor.Max[0] = -SmallestX;
			Accessor.Min[2] = -Accessor.Max[2];
			Accessor.Max[2] = -SmallestZ;
		}
	}

	// The nodes last, because they depend on no buffer: they are numbers in the
	// JSON itself. The scale stays as it is -- a half turn changes no size.
	for (FGLTFJsonNode* Node : Root.Nodes)
	{
		Node->Translation.X = -Node->Translation.X;
		Node->Translation.Z = -Node->Translation.Z;
		Node->Rotation.X = -Node->Rotation.X;
		Node->Rotation.Z = -Node->Rotation.Z;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
