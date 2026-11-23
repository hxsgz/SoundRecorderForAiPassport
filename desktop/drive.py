"""Windows Explorer view over locally verified WAV copies; uses a per-user DOS drive."""

import ctypes
import os
import subprocess


def mapping(letter):
    buffer = ctypes.create_unicode_buffer(32768)
    if ctypes.windll.kernel32.QueryDosDeviceW(letter, buffer, len(buffer)):
        return buffer.value
    return None


class ExplorerDrive:
    def __init__(self, folder, preferred="R"):
        self.folder = folder.resolve()
        self.letter = None
        if os.name != "nt":
            return
        target = "\\??\\" + str(self.folder)
        letters = dict.fromkeys(
            preferred.upper().strip(":") + "RSTUVWXYZQPONMLKJIHGFED"
        )
        for letter in letters:
            if not "D" <= letter <= "Z":
                continue
            name = letter + ":"
            old = mapping(name)
            if old and old.casefold() == target.casefold():
                self.letter = name
                break
            if old is None:
                result = subprocess.run(
                    ["subst", name, str(self.folder)], capture_output=True
                )
                if result.returncode == 0:
                    self.letter = name
                    break

    def close(self):
        if self.letter:
            expected = "\\??\\" + str(self.folder)
            if (mapping(self.letter) or "").casefold() == expected.casefold():
                subprocess.run(["subst", self.letter, "/D"], capture_output=True)
            self.letter = None
