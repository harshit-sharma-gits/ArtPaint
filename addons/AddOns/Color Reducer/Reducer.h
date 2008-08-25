/* 

	Filename:	Reducer.h
	Contents:	Declarations for brightness add-on.	
	Author:		Heikki Suhonen
	
*/


#ifndef THRESHOLD_H
#define THRESHOLD_H

#include "WindowGUIManipulator.h"
#include "Controls.h"


enum {
	NO_DITHER,
	FLOYD_STEINBERG_EDD_DITHER,
	ORDERED_DITHER,
	N_CANDIDATE_DITHER,
	PRESERVE_SOLIDS_DITHER
};


enum {
	BEOS_PALETTE,
	GLA_PALETTE
};

class	ReducerManipulatorSettings : public ManipulatorSettings {
public:
		ReducerManipulatorSettings()
			: ManipulatorSettings() {
			dither_mode = FLOYD_STEINBERG_EDD_DITHER;
			palette_size = 256;
			palette_mode = BEOS_PALETTE;
		}

		ReducerManipulatorSettings(const ReducerManipulatorSettings& s)
			: ManipulatorSettings() {
			dither_mode = s.dither_mode;
			palette_size = s.palette_size;
			palette_mode = s.palette_mode;
		}


		ReducerManipulatorSettings& operator=(const ReducerManipulatorSettings& s) {
			dither_mode = s.dither_mode;
			palette_size = s.palette_size;
			palette_mode = s.palette_mode;
			return *this;
		}


		bool operator==(ReducerManipulatorSettings s) {
			return ((dither_mode == s.dither_mode)
					&& (palette_size == s.palette_size)
					&& (palette_mode == s.palette_mode));
		}
		
		bool operator!=(ReducerManipulatorSettings s) {
			return !(*this==s);
		}

int32	dither_mode;
int32	palette_size;
int32	palette_mode;
};


class ReducerManipulatorView;

class ReducerManipulator : public WindowGUIManipulator {
			BBitmap	*preview_bitmap;
			BBitmap	*copy_of_the_preview_bitmap;
						
			ReducerManipulatorSettings	settings;
			ReducerManipulatorSettings	previous_settings;
			
			ReducerManipulatorView		*config_view;

void		do_dither(BBitmap*,BBitmap*,const rgb_color *palette,int palette_size,int32 dither_mode);


public:
			ReducerManipulator(BBitmap*);
			~ReducerManipulator();
			
void		MouseDown(BPoint,uint32 buttons,BView*,bool);
int32		PreviewBitmap(Selection*,bool full_quality=FALSE,BRegion* =NULL);
BBitmap*	ManipulateBitmap(ManipulatorSettings*,BBitmap*,Selection*,BStatusBar*);	
void		Reset(Selection*);
void		SetPreviewBitmap(BBitmap*);
char*		ReturnHelpString();
char*		ReturnName();

ManipulatorSettings*	ReturnSettings();

BView*		MakeConfigurationView(BMessenger*);

void		ChangeSettings(ManipulatorSettings*);		
};



#define	DITHER_MODE_CHANGED		'Dmoc'
#define	PALETTE_SIZE_CHANGED	'Plsc'
#define PALETTE_MODE_CHANGED	'Plmc'

class ReducerManipulatorView : public WindowGUIManipulatorView {
		BMessenger						target;
		ReducerManipulator			*manipulator;
		ReducerManipulatorSettings	settings;	
		
		BMenuField		*dither_mode_menu_field;
		BMenuField		*palette_size_menu_field;
		BMenuField		*palette_mode_menu_field;
		
public:
		ReducerManipulatorView(ReducerManipulator*,BMessenger*);

void	AllAttached();
void	AttachedToWindow();
void	MessageReceived(BMessage*);
void	ChangeSettings(ManipulatorSettings*);
};

#endif



