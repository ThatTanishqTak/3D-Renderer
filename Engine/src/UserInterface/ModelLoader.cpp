#include "ModelLoader.h"
#include <iostream>

#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include <gui_window_file_dialog.h>

namespace Engine
{
	GuiWindowFileDialogState fileDialogState = InitGuiWindowFileDialog(GetWorkingDirectory());

	void ModelLoader::Init()
	{

	}

	void ModelLoader::SHutdown()
	{

	}

	void ModelLoader::Update()
	{

	}

	void ModelLoader::Load()
	{

	}

	void ModelLoader::Unload()
	{

	}

	void ModelLoader::OpenFileDialog()
	{
		if (fileDialogState.SelectFilePressed)
		{
			// Load image file (if supported extension)
			if (IsFileExtension(fileDialogState.fileNameText, ".png"))
			{

			}

			fileDialogState.SelectFilePressed = false;
		}

		if (fileDialogState.windowActive)
		{
			GuiLock();
		}

		if (GuiButton({ 20, 20, 140, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Image")))
		{
			fileDialogState.windowActive = true;
		}

		GuiUnlock();
		GuiWindowFileDialog(&fileDialogState);
	}
}