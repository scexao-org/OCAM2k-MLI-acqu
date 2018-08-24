# Pyramid Camera Acquisition

Pyramid Camera Data Acquisition (computer scexaoPCAM)

Reads ocam2k camera with Matrox frame grabber. Transfers data through TCP link to RTC computer. Pixel values are transfered as they are read, without waiting for full frame.


## Pre-requisites:

- Matrox Imaging Library
- First Light Imaging SDK for OCAM2k
- cacao or milk
- tmux

## Compiling

	./compile

Creates executable ocamrun

## Running

High level scripts sets up connection and runs in tmux session:

	./ocamstart

For more information, run:

	./ocamstart -h


