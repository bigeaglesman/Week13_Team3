#include "EnginePCH.h"
#include "Editor/Viewports/EditorViewportToolbar.h"
#include "SkeletalMeshViewportPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"

FSkeletalMeshViewportPanel::FSkeletalMeshViewportPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Viewport")
{
}

void FSkeletalMeshViewportPanel::DrawContent()
{

	FEditorViewportClient* Client = Editor.GetViewportClient();
	const float RowStart = ImGui::GetCursorPosX();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	const float RowRight = ImGui::GetCursorScreenPos().x + RowWidth;
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float ModeWidth = 2.0f * (ImGui::GetFrameHeight() + Style.ItemInnerSpacing.x)
		+ ImGui::CalcTextSize("Mesh").x + ImGui::CalcTextSize("Physics Asset").x + Style.ItemSpacing.x;
	ImGui::SetCursorPosX(RowStart + std::max(0.0f, RowWidth - ModeWidth));
	const float ModeLeft = ImGui::GetCursorScreenPos().x;
	const bool bWasPhysics = Editor.IsPhysicsAssetMode();
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
	ImGui::PushStyleColor(ImGuiCol_Border, Style.Colors[ImGuiCol_TextDisabled]);
	if (ImGui::RadioButton("Mesh", !bWasPhysics) && bWasPhysics)
		Editor.SetPhysicsAssetMode(false);
	ImGui::SameLine();
	if (ImGui::RadioButton("Physics Asset", bWasPhysics) && !bWasPhysics)
		Editor.SetPhysicsAssetMode(true);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
	if (Editor.IsPhysicsAssetMode())
	{
		const bool bPlaying = Editor.IsPhysicsPreviewRunning();
		const float SaveWidth = ImGui::CalcTextSize("Save Physics Asset").x + 2.0f * Style.FramePadding.x;
		const float ActionWidth = ImGui::CalcTextSize("Simulate").x + 2.0f * Style.FramePadding.x;
		const float ActionsWidth = SaveWidth + Style.ItemSpacing.x + ActionWidth;
		const bool bSharedRow = RowWidth >= ActionsWidth + Style.ItemSpacing.x + ModeWidth;
		if (bSharedRow)
		{
			ImGui::SameLine();
			ImGui::SetCursorPosX(RowStart);
		}
		if (ImGui::Button(bPlaying ? "Stop" : "Simulate", ImVec2(ActionWidth, 0)))
		{
			if (bPlaying) Editor.StopPhysicsPreview();
			else Editor.StartPhysicsPreview();
		}
		const bool bSimulateHovered = ImGui::IsItemHovered();
		if (RowWidth >= ActionsWidth)
			ImGui::SameLine();
		ImGui::BeginDisabled(Editor.IsPhysicsPreviewRunning() || !Editor.GetMesh()->GetPhysicsAsset());
		if (ImGui::Button("Save Physics Asset"))
			Editor.SavePhysicsAsset();
		ImGui::EndDisabled();
		const FString& Status = Editor.GetPhysicsAssetStatus();
		if (!Status.empty())
		{
			const ImVec2 ButtonMax = ImGui::GetItemRectMax();
			const ImVec2 TextMin(ButtonMax.x + Style.ItemSpacing.x, ImGui::GetItemRectMin().y + Style.FramePadding.y);
			const float RightEdge = bSharedRow ? ModeLeft - Style.ItemSpacing.x : RowRight;
			const ImVec2 TextSize = ImGui::CalcTextSize(Status.c_str());
			const ImVec2 TextMax(std::min(RightEdge, TextMin.x + TextSize.x), ButtonMax.y);
			bool bStatusHovered = false;
			if (TextMax.x > TextMin.x)
			{
				ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), TextMin, TextMax, TextMax.x,
					Status.data(), Status.data() + Status.size(), &TextSize);
				bStatusHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(TextMin, TextMax);
			}
			if (bStatusHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || (TextMax.x <= TextMin.x && bSimulateHovered))
				ImGui::SetTooltip("%s", Status.c_str());
		}
	}

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewMax(Origin.x + Size.x, Origin.y + Size.y);
	if (FTexture2D* Target = Client->Viewport ? Client->Viewport->GetRenderTargetTexture() : nullptr; Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ViewMax);
	}

	// UE5식 뷰포트 툴바(기즈모 도구, 뷰 종류, 카메라, 뷰 모드, 쇼 플래그)와 왼쪽 아래 축 표시.
	// 입력 영역보다 먼저 그려야 툴바 버튼이 마우스를 먼저 받는다. 툴바 위에서는 뷰포트 입력을 막는다.
	const unsigned GizmoModes = Editor.GetGizmoModeMask();
	const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax, Editor.GetGizmoMode(),
		[this](UE::Widget::EWidgetMode Mode) { Editor.SetGizmoMode(Mode); }, GizmoModes);

	// 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지한다.
	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##SkeletalMeshViewport", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);


	const bool bHovered = ImGui::IsItemHovered() && !bToolbarBusy;
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		ImGui::SetWindowFocus();
	}

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered)
		EditorViewportUI::HandleViewTypeShortcuts(Client);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		Editor.FocusMesh();
	}


	// 분리된 플랫폼 창의 입력도 받을 수 있도록 ImGui 입력을 사용합니다.
	//
	// TODO: 여기서도 FInputSystem을 사용할 수 있도록, 구조 개선하기
	// ImGui가 생성하는 HWND에 직접 WndProc을 연결하고 FInputSystem에 전달시켜야함

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const FViewportPointerInput PointerInput
	{
		.MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
		.bHovered = bHovered,
		.bFocused = bFocused && !IO.AppFocusLost,
		.bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
		.bMouseDown = bMouseDown,
		.bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
	};

	FViewportCameraInput Input
	{
		.bCaptured		= bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !Editor.IsGizmoDragging(),
		.bAllowWheel	= bHovered && !bMouseDown && !Editor.IsGizmoDragging(),
		.bForward		= ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward		= ImGui::IsKeyDown(ImGuiKey_S),
		.bRight			= ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft			= ImGui::IsKeyDown(ImGuiKey_A),
		.bUp			= ImGui::IsKeyDown(ImGuiKey_E),
		.bDown			= ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX			= IO.MouseDelta.x,
		.MouseY			= IO.MouseDelta.y,

		// ImGui의 휠 단위를 기존 카메라 이동 함수가 사용하는 Windows 휠 단위로 변환합니다.
		.Wheel			= IO.MouseWheel * 120.0f,
	};

	// 키보드로 Gizmo 키 변경
	if (GizmoModes && bFocused && !IO.WantTextInput && !IO.AppFocusLost &&
		!ImGui::IsAnyItemActive() && !bCameraDragging && !Editor.IsGizmoDragging())
	{
		using namespace UE::Widget;
		if (ImGui::Shortcut(ImGuiKey_W))
		{
			Editor.SetGizmoMode(WM_Translate);
		}
		if ((GizmoModes & ModeBit(WM_Rotate)) && ImGui::Shortcut(ImGuiKey_E))
		{
			Editor.SetGizmoMode(WM_Rotate);
		}
		if ((GizmoModes & ModeBit(WM_Scale)) && ImGui::Shortcut(ImGuiKey_R))
		{
			Editor.SetGizmoMode(WM_Scale);
		}
		if (ImGui::Shortcut(ImGuiKey_Space))
		{
			for (int32 Step = 1; Step <= WM_Max; ++Step)
			{
				const auto Next = static_cast<EWidgetMode>((Editor.GetGizmoMode() + Step) % WM_Max);
				if (GizmoModes & ModeBit(Next))
				{
					Editor.SetGizmoMode(Next);
					break;
				}
			}
		}
	}

	// 렌더 타깃 변경은 Editor의 다음 Render에서 수행합니다.
	uint32 Width = static_cast<uint32>(Size.x);
	uint32 Height = static_cast<uint32>(Size.y);
	Editor.SetViewportState(Width, Height, Input, PointerInput);
}
