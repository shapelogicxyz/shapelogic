---
title: When ShapeLogic does not start on Windows
description: What the engine's messages mean, how to restore or allow ShapeLogic in Windows Security and other antivirus programs, and how to send the diagnostics.
---

ShapeLogic runs its puzzle engine, `shapelogic_engine.exe`, as a program of
its own beside the window. When Windows or security software keeps the
engine from starting, the window opens but cannot open or make a puzzle.
ShapeLogic then shows **The engine could not start** with the likely cause.
This page explains the causes and what to try.

## What the messages mean

| The message says | What it means |
|---|---|
| Windows refused to start the engine | Windows gave no reason (`spawn UNKNOWN`). This is usually security software blocking a program that is not signed: an antivirus, Smart App Control, or a company's application control. A damaged engine file can cause it too. |
| The engine's file is missing | The file `shapelogic_engine.exe` is gone (`ENOENT`). Security software may have quarantined it, or the install is incomplete. |
| Windows denied running the engine | Security software or the file's permissions do not allow it to run (`EACCES`, `EPERM`). |
| The engine file is locked | Often an antivirus scanning it (`EBUSY`). Wait a moment and choose **Try Again**. |
| The engine started but did not answer | It did not answer within 10 seconds (`TIMEOUT`). Security software may be holding it. |
| Another version of the engine is installed | The engine file is from another release. Install ShapeLogic again. |
| A library the engine needs is missing | The engine started and ended at once with a Windows status code. The dialog names the code. |

## Restore or allow ShapeLogic in Windows Security

1. Open **Windows Security** from the Start menu.
2. Choose **Virus & threat protection ▸ Protection history**.
3. Look for an entry that names `ShapeLogic` or `shapelogic_engine.exe`.
4. If it was quarantined or blocked, choose **Actions ▸ Restore**, or
   **Allow on device**.
5. In ShapeLogic, choose **Try Again**, or start it again.

If you use another antivirus program, look in its quarantine or its
history in the same way, and add ShapeLogic's folder to its exceptions.

## Smart App Control

Smart App Control (Windows 11) blocks programs that are not signed or not
known to Microsoft. While it is **On**, it may stop the engine. It is in
**Windows Security ▸ App & browser control ▸ Smart App Control settings**.
Once it is turned off, Windows does not let you turn it on again without
reinstalling Windows, so read Microsoft's notes before you change it.

## Controlled Folder Access

Controlled Folder Access (**Windows Security ▸ Virus & threat protection ▸
Ransomware protection**) keeps programs it does not know from writing into
Documents, Desktop, Pictures and other folders. ShapeLogic then starts, but
a save or an export fails as *access denied*. ShapeLogic says so when that
happens. Allow ShapeLogic in **Allow an app through Controlled folder
access**, or save to another folder.

## Send the diagnostics

**Help ▸ Copy Diagnostics** (or **Copy Details** in the engine's dialog)
puts a report on the clipboard. It says which Windows this is, where
ShapeLogic is installed, whether its engine file is there and unchanged,
why the engine could not start, and the newest lines of ShapeLogic's log.
On Windows it also asks Windows what Microsoft Defender, Smart App Control
and AppLocker recorded about ShapeLogic's files, and which antivirus
programs are installed. These checks run only when you ask for the report.

The report names only ShapeLogic's own files, your system's version and
your security software. Your home folder is written as `~`. Paste it into
a [GitHub issue](https://github.com/shapelogicxyz/shapelogic/issues) or an
email.
