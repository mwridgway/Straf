# Project Title

A program that monitors mic audio on the computer to detect words that are expletives and penalises the user
when one is detected.  

## Getting Started

Run the app from the command line, or install it as a privileged windows service.

## Features

- The user can provide a list of words to detect that will result in a penalty
- Penalties are enable when a word in the list is detected.  Each penalty has a cooling off period.
- If another word is detected while a penalty is active, another penalty is added that must be served after any current penalty is being served - up to a limit of 5 active penalties.
- Penalties are in the form of a screen overlay visible above all other user interface elements, even during a game or other full screen application.  This should be in the style of GTA wanted levels visually.

## Technical implementation

- The app is written in C or C++ for optimal performance
- Compatible with Windows 11
- Can run in the background as a service

## License

This project is licensed under the MIT License - see the LICENSE file for details.
