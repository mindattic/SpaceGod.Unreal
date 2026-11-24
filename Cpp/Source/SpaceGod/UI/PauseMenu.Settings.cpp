#include "UI/PauseMenu.h"

#include "Settings/GameSetting.h"
#include "UI/UiInput.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	bool PauseMenu::TextKeys()
	{
		if (textEditing == nullptr) return false;
		if (GUI::GetNameOfFocusedControl() != cs::Cat("SettingText.", textEditing->Key))
		{
			DropText(); // the field let go (a click elsewhere) while its row was out of sight
			return false;
		}
		if (!UiInput::WasPressedThisFrame(KeyCode::Return) && !UiInput::WasPressedThisFrame(KeyCode::KeypadEnter)) return true;
		if (textEditing->Set(textDraft))
		{
			GUIUtility::keyboardControl = 0;
			DropText();
			return true;
		}
		int chars = 0; // UTF-8 code points, as TextSetting counts them
		for (char c : textDraft)
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) chars++;
		textRefusedKey = textEditing->Key;
		textRefused = chars > textEditing->MaxLength
			? cs::Cat("Not taken: at most ", textEditing->MaxLength, " characters")
			: cs::String("Not taken: that value is refused");
		return true;
	}

	void PauseMenu::DropText()
	{
		if (textEditing != nullptr && GUI::GetNameOfFocusedControl() == cs::Cat("SettingText.", textEditing->Key))
			GUIUtility::keyboardControl = 0;
		textEditing = nullptr;
		textDraft.clear();
		textRefusedKey.clear();
		textRefused.clear();
	}
}
