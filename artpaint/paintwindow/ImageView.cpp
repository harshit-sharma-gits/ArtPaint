/*
 * Copyright 2003, Heikki Suhonen
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Heikki Suhonen <heikki.suhonen@gmail.com>
 *
 */

#include <Alert.h>
#include <ClassInfo.h>
#include <Clipboard.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <new>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollBar.h>
#include <Slider.h>
#include <StatusBar.h>
#include <Window.h>

#include "Cursors.h"
#include "DrawingTools.h"
#include "Image.h"
#include "ImageAdapter.h"
#include "ImageView.h"
#include "Layer.h"
#include "LayerWindow.h"
#include "Manipulator.h"
#include "ManipulatorServer.h"
#include "ManipulatorWindow.h"
#include "MessageConstants.h"
#include "PaintApplication.h"
#include "PaintWindow.h"
#include "Patterns.h"
#include "ProjectFileFunctions.h"
#include "Selection.h"
#include "SettingsServer.h"
#include "StatusBarGUIManipulator.h"
#include "StatusView.h"
#include "TextManipulator.h"
#include "ToolManager.h"
#include "Tools.h"
#include "UndoQueue.h"
#include "UtilityClasses.h"
#include "WindowGUIManipulator.h"
#include "StringServer.h"

ImageView::ImageView(BRect frame, float width, float height)
		: BView(frame,"image_view",B_FOLLOW_NONE,B_WILL_DRAW)
{
	// Initialize the undo-queue
	undo_queue = new UndoQueue(NULL,NULL,this);

// This will only be set once after reafing the prefs-file
//	UndoQueue::SetQueueDepth(((PaintApplication*)be_app)->GlobalSettings()->undo_queue_depth);

	// Initialize the image.
	the_image = new Image(this,width,height,undo_queue);
	current_display_mode = FULL_RGB_DISPLAY_MODE;

	// here set the magnify_scale for image
	magnify_scale = 1.0;

	// here we set the grid
	setGrid(BPoint(0,0),1);

	// We are not yet inside any actions, so...
	mouse_mutex = create_sem(1,"mouse_mutex");
	action_semaphore = create_sem(1,"action_semaphore");

	// Set the correct color for view (i.e. B_TRANSPARENT_32_BIT)
	SetViewColor(B_TRANSPARENT_32_BIT);

	fManipulator = NULL;
	manipulator_window = NULL;
	manipulator_finishing_message = NULL;

	// Create an empty selection.
	selection = new Selection(BRect(0,0,width-1,height-1));
	selection->StartDrawing(this,magnify_scale);

	cursor_mode = NORMAL_CURSOR_MODE;
	manipulated_layers = HS_MANIPULATE_NO_LAYER;

	// Initialize the magnify-scale array
	mag_scale_array_length = 15;
	mag_scale_array = new float[mag_scale_array_length];

	mag_scale_array[0] = 0.10;
	mag_scale_array[1] = 0.125;
	mag_scale_array[2] = 0.175;
	mag_scale_array[3] = 0.25;
	mag_scale_array[4] = 0.35;
	mag_scale_array[5] = 0.50;
	mag_scale_array[6] = 0.65;
	mag_scale_array[7] = 0.80;
	mag_scale_array[8] = 1.00;
	mag_scale_array[9] = 1.50;
	mag_scale_array[10] = 2.00;
	mag_scale_array[11] = 3.00;
	mag_scale_array[12] = 4.00;
	mag_scale_array[13] = 8.00;
	mag_scale_array[14] = 16.00;

	mag_scale_array_index = -1;	// This has not yet been decided

	reference_point = BPoint(0,0);
	use_reference_point = FALSE;

	project_changed = 0;
	image_changed = 0;

	project_name = NULL;
	image_name = NULL;

	AddFilter(new BMessageFilter(B_KEY_DOWN, KeyFilterFunction));
}


ImageView::~ImageView()
{
	// Here we free all allocated memory.

	// Delete the view manipulator and close it's possible window.
	if (manipulator_window != NULL) {
		manipulator_window->Lock();
		manipulator_window->Close();
	}
	delete fManipulator;

	// Delete the undo-queue
	delete undo_queue;

	// Delete the selection
	delete selection;

	// Delete the image
	delete the_image;

	delete[] mag_scale_array;

	// Delete the semaphores
	delete_sem(mouse_mutex);
	delete_sem(action_semaphore);
}

void ImageView::AttachedToWindow()
{
	// Set the type (dithered or full color).
	BScreen screen(Window());
	if (screen.ColorSpace() == B_CMAP8)
		SetDisplayMode(DITHERED_8_BIT_DISPLAY_MODE);
	else
		SetDisplayMode(FULL_RGB_DISPLAY_MODE);

	// Make the composite picture (and also update miniature images).
	the_image->Render();

	// Initialize the undo-queue
	undo_queue->SetMenuItems(Window()->KeyMenuBar()->FindItem(HS_UNDO),Window()->KeyMenuBar()->FindItem(HS_REDO));


	// Make this view the focus-view (receives key-down events).
	MakeFocus();
}

void ImageView::Draw(BRect updateRect)
{
	// copy image from bitmap to the part requiring updating

	SetDrawingMode(B_OP_COPY);

	BRegion a_region;
	GetClippingRegion(&a_region);
	for (int32 i=0;i<a_region.CountRects();i++) {
		BlitImage(convertViewRectToBitmap(a_region.RectAt(i) & updateRect));
	}

	// Draw the selection also
	selection->Draw();

	// Make the manipulator draw it's UI here.
	DrawManipulatorGUI(FALSE);

	// finally Flush() after drawing asynchronously
	Flush();

}


void ImageView::BlitImage(BRect bitmap_rect)
{
	BRect image_rect;
	float mag_scale = getMagScale();

	BBitmap *source_bitmap;
	if ((current_display_mode == FULL_RGB_DISPLAY_MODE) || (the_image->IsDitheredUpToDate() == FALSE))
		source_bitmap = the_image->ReturnRenderedImage();
	else
		source_bitmap = the_image->ReturnDitheredImage();

	if (mag_scale == 1.0) {
		image_rect = bitmap_rect;
		DrawBitmapAsync(source_bitmap,image_rect,bitmap_rect);
	}
	else if (mag_scale != 1.0) {
		image_rect = convertBitmapRectToView(bitmap_rect);
		DrawBitmapAsync(source_bitmap,bitmap_rect,image_rect);
	}
}


void ImageView::UpdateImage(BRect bitmap_rect)
{
	bitmap_rect = bitmap_rect & the_image->ReturnRenderedImage()->Bounds();
	the_image->Render(bitmap_rect);
	BlitImage(bitmap_rect);
}


void ImageView::DrawManipulatorGUI(bool blit_image)
{
	GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
	if (gui_manipulator != NULL) {
		if (blit_image == TRUE) {
			for (int32 i=0;i<region_drawn_by_manipulator.CountRects();i++) {
				BlitImage(convertViewRectToBitmap(region_drawn_by_manipulator.RectAt(i)));
			}
		}

		region_drawn_by_manipulator = gui_manipulator->Draw(this,getMagScale());
	}
	else {
		region_drawn_by_manipulator = BRegion();
	}
}


void ImageView::KeyDown(const char *bytes, int32 numBytes)
{
	if (*bytes == B_LEFT_ARROW) {
		BRect bounds = Bounds();
		float delta = bounds.Width() / 2;
		if (bounds.left - delta < 0) delta = bounds.left;
		ScrollBy(-delta, 0);
	}
	else if (*bytes == B_UP_ARROW) {
		BRect bounds = Bounds();
		float delta = bounds.Height() / 2;
		if (bounds.top - delta < 0) delta = bounds.top;
		ScrollBy(0, -delta);
	}
	else if (*bytes == B_RIGHT_ARROW) {
		BRect bounds = Bounds();
		float delta = bounds.Width() / 2;
		BRect bitmap_rect = convertBitmapRectToView(the_image->ReturnRenderedImage()->Bounds());
		if (bounds.right + delta > bitmap_rect.right) delta = bitmap_rect.right - bounds.right;
		ScrollBy(delta, 0);
	}
	else if (*bytes == B_DOWN_ARROW) {
		BRect bounds = Bounds();
		float delta = bounds.Height() / 2;
		BRect bitmap_rect = convertBitmapRectToView(the_image->ReturnRenderedImage()->Bounds());
		if (bounds.bottom + delta > bitmap_rect.bottom) delta = bitmap_rect.bottom - bounds.bottom;
		ScrollBy(0, delta);
	}
	else if (fManipulator == NULL) {
		ToolManager::Instance().KeyDown(this,bytes,numBytes);
	}
}

void ImageView::MessageReceived(BMessage *message)
{
	BMessage message_to_window;
	BMessage message_to_app;

	// here check what the message is all about and initiate proper action
	switch (message->what) {
		// This comes from layer's view and tells us to change the active layer.
		case HS_LAYER_ACTIVATED:
			// Only change the layer if we can acquire the action_semaphore.
			if (acquire_sem_etc(action_semaphore,1,B_TIMEOUT,0) == B_OK) {
				int32 activated_layer_id;
				Layer *activated_layer;
				message->FindInt32("layer_id",&activated_layer_id);
				message->FindPointer("layer_pointer",(void**)&activated_layer);
				if (the_image->ChangeActiveLayer(activated_layer,activated_layer_id) == TRUE)
					ActiveLayerChanged();

				release_sem(action_semaphore);
			}
			break;

		case HS_ZOOM_IMAGE_IN: {
			if (mag_scale_array_index == -1) {
				// Search for the right index.
				mag_scale_array_index = 0;
				while ((mag_scale_array_index < mag_scale_array_length - 1)
					&& (mag_scale_array[mag_scale_array_index] <= magnify_scale)) {
					mag_scale_array_index++;
				}
			} else {
				mag_scale_array_index = min_c(mag_scale_array_index + 1,
					mag_scale_array_length - 1);
			}

			setMagScale(mag_scale_array[mag_scale_array_index]);
		}	break;

		case HS_ZOOM_IMAGE_OUT: {
			if (mag_scale_array_index == -1) {
				// Search for the right index.
				mag_scale_array_index = mag_scale_array_length - 1;
				while ((mag_scale_array_index > 0)
					&& (mag_scale_array[mag_scale_array_index] >= magnify_scale)) {
					mag_scale_array_index--;
				}
			} else {
				mag_scale_array_index = max_c(mag_scale_array_index - 1, 0);
			}

			setMagScale(mag_scale_array[mag_scale_array_index]);
		}	break;

		case HS_SET_MAGNIFYING_SCALE: {
			mag_scale_array_index = -1;
			float newMagScale;
			if (message->FindFloat("magnifying_scale", &newMagScale) == B_OK) {
				setMagScale(newMagScale);
				((PaintWindow*)Window())->displayMag(newMagScale);
			} else {
				void *source;
				if (message->FindPointer("source", &source) == B_OK) {
					BSlider *slider = static_cast<BSlider*> (source);
					if (slider == NULL)
						break;

					// Convert nonlinear from [10,1600] -> [0.1,16]
					float scale = (pow(10.0, (slider->Value() - 10.0) / 1590.0)
						- 1.0) * (15.9 / 9.0) + 0.1;
					setMagScale(scale);
				}
			}
		}	break;

		case HS_GRID_ADJUSTED: {
			BPoint origin;
			if (message->FindPoint("origin",&origin) == B_OK) {
				int32 unit;
				if (message->FindInt32("unit",&unit) == B_OK) {
					grid_unit = unit;
					grid_origin = origin;
				}
			}
		}	break;

		// This comes from layer's view and tells us to change the visibility for that layer.
		case HS_LAYER_VISIBILITY_CHANGED:
			int32 changed_layer_id;
			Layer *changed_layer;
			message->FindInt32("layer_id",&changed_layer_id);
			message->FindPointer("layer_pointer",(void**)&changed_layer);
			if (the_image->ToggleLayerVisibility(changed_layer,changed_layer_id) == TRUE) {
				Invalidate();
				AddChange();	// Is this really necessary?
			}
			break;

		// This comes from layer's view and tells us to move layer to another position in the list.
		case HS_LAYER_POSITION_CHANGED:
			int32 positions_moved;
			message->FindInt32("layer_id",&changed_layer_id);
			message->FindPointer("layer_pointer",(void**)&changed_layer);
			message->FindInt32("positions_moved",&positions_moved);
			if (the_image->ChangeLayerPosition(changed_layer,changed_layer_id,positions_moved) == TRUE) {
				Invalidate();
				AddChange();
			}
			break;

		// This comes from layer's view and tells us to delete the layer.
		case HS_DELETE_LAYER:
			{
				Layer *removed_layer;
				int32 removed_layer_id;
				message->FindInt32("layer_id",&removed_layer_id);
				message->FindPointer("layer_pointer",(void**)&removed_layer);
				if (the_image->RemoveLayer(removed_layer,removed_layer_id) == TRUE) {
					ActiveLayerChanged();
					AddChange();
				}
				LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
				Invalidate();
				break;
			}

		// This comes from layer's view and tells us to merge that layer with the one
		// that is on top of it. If no layer is on top of it nothing should be done.
		// The merged layer will then be made visible if necessary. But the active layer
		// stays the same unless one of the merged layers was the active layer
		case HS_MERGE_WITH_UPPER_LAYER:
			if (!PostponeMessageAndFinishManipulator()) {
				Layer *merged_layer;
				int32 merged_layer_id;
				message->FindInt32("layer_id",&merged_layer_id);
				message->FindPointer("layer_pointer",(void**)&merged_layer);
				if (the_image->MergeLayers(merged_layer,merged_layer_id,TRUE) == TRUE) {
					LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
					Invalidate();
					AddChange();
				}
			}
			break;

		// This comes from layer's view and tells us to merge that layer with the one
		// that is under it. If no layer is under it nothing should be done.
		// The merged layer will then be made visible if necessary. But the active layer
		// stays the same unless one of the merged layers was the active layer
		case HS_MERGE_WITH_LOWER_LAYER:
			if (!PostponeMessageAndFinishManipulator()) {
				Layer *merged_layer;
				int32 merged_layer_id;
				message->FindInt32("layer_id",&merged_layer_id);
				message->FindPointer("layer_pointer",(void**)&merged_layer);
				if (the_image->MergeLayers(merged_layer,merged_layer_id,FALSE) == TRUE) {
					LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
					Invalidate();
					AddChange();
				}
			}
			break;

		case HS_ADD_LAYER_FRONT:
		case HS_ADD_LAYER_BEHIND:
			{
				Layer *other_layer;
				message->FindPointer("layer_pointer",(void**)&other_layer);
				try {
					the_image->AddLayer(NULL,other_layer,message->what == HS_ADD_LAYER_FRONT);
					LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
					ActiveLayerChanged();
					Invalidate();
					AddChange();
				}
				catch (std::bad_alloc){
					ShowAlert(CANNOT_ADD_LAYER_ALERT);
				}
				break;
			}


		case HS_DUPLICATE_LAYER:
			{
				Layer *duplicated_layer;
				int32 duplicated_layer_id;
				message->FindInt32("layer_id",&duplicated_layer_id);
				message->FindPointer("layer_pointer",(void**)&duplicated_layer);
				try {
					if (the_image->DuplicateLayer(duplicated_layer,duplicated_layer_id) == TRUE) {
						LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
						ActiveLayerChanged();
						Invalidate();
						AddChange();
					}
				}
				catch (std::bad_alloc e) {
					ShowAlert(CANNOT_ADD_LAYER_ALERT);
				}
				break;
			}
		// This comes when a layer's miniature image is dragged on the view.
		// We should copy the image from that layer and add it to this image.
		case HS_LAYER_DRAGGED:
			if (message->WasDropped()) {
				BBitmap *to_be_copied;
				if (message->FindPointer("layer_bitmap",(void**)&to_be_copied) == B_OK) {
					try {
						BBitmap *new_bitmap = new BBitmap(to_be_copied);
						if (the_image->AddLayer(new_bitmap,NULL,TRUE) != NULL) {
							Invalidate();
							LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
							AddChange();
						}
					}
					catch (std::bad_alloc) {
						ShowAlert(CANNOT_ADD_LAYER_ALERT);
					}
				}
			}
			break;

		// this comes from menubar->"Edit"->"Invert Selection", we should then invert the
		// selected area and redisplay it
		case HS_INVERT_SELECTION:
			selection->Invert();
			if (!(*undo_queue->ReturnSelectionData() == *selection->ReturnSelectionData())) {
				UndoEvent *new_event = undo_queue->AddUndoEvent(StringServer::ReturnString(INVERT_SELECTION_STRING),the_image->ReturnThumbnailImage());
				if (new_event != NULL) {
					new_event->SetSelectionData(undo_queue->ReturnSelectionData());
					undo_queue->SetSelectionData(selection->ReturnSelectionData());
				}
			}

			Invalidate();
			break;

		// this comes from menubar->"Edit"->"Clear Selection", we should then clear the
		// selection and redisplay the image
		case HS_CLEAR_SELECTION:
			selection->Clear();
			if (!(*undo_queue->ReturnSelectionData() == *selection->ReturnSelectionData())) {
				UndoEvent *new_event = undo_queue->AddUndoEvent(StringServer::ReturnString(CLEAR_SELECTION_STRING),the_image->ReturnThumbnailImage());
				if (new_event != NULL) {
					new_event->SetSelectionData(undo_queue->ReturnSelectionData());
					undo_queue->SetSelectionData(selection->ReturnSelectionData());
				}
			}
			Invalidate();
			break;

		case HS_GROW_SELECTION:
			selection->Dilatate();
			if (!(*undo_queue->ReturnSelectionData() == *selection->ReturnSelectionData())) {
				UndoEvent *new_event = undo_queue->AddUndoEvent(StringServer::ReturnString(GROW_SELECTION_STRING),the_image->ReturnThumbnailImage());
				if (new_event != NULL) {
					new_event->SetSelectionData(undo_queue->ReturnSelectionData());
					undo_queue->SetSelectionData(selection->ReturnSelectionData());
				}
			}
			Invalidate();
			break;

		case HS_SHRINK_SELECTION:
			selection->Erode();
			if (!(*undo_queue->ReturnSelectionData() == *selection->ReturnSelectionData())) {
				UndoEvent *new_event = undo_queue->AddUndoEvent(StringServer::ReturnString(SHRINK_SELECTION_STRING),the_image->ReturnThumbnailImage());
				if (new_event != NULL) {
					new_event->SetSelectionData(undo_queue->ReturnSelectionData());
					undo_queue->SetSelectionData(selection->ReturnSelectionData());
				}
			}
			Invalidate();
			break;

		// this comes from menubar->"Canvas"->"Clear Canvas", we should then clear all the
		// layers and recalculate the composite picture and redisplay
		case HS_CLEAR_CANVAS:
			if (acquire_sem_etc(action_semaphore,1,B_TIMEOUT,0) == B_OK) {
				rgb_color c = ((PaintApplication*)be_app)->Color(FALSE);
				if (the_image->ClearLayers(c) == TRUE) {
					Invalidate();
					AddChange();
				}
				release_sem(action_semaphore);
			}
			break;

		// This comes from menubar->"Layer"->"Clear Layer". We should clear the layer, recalculate
		// the composite picture and redisplay the image.
		case HS_CLEAR_LAYER:
			if (acquire_sem_etc(action_semaphore,1,B_TIMEOUT,0) == B_OK) {
				rgb_color c = ((PaintApplication*)be_app)->Color(FALSE);
				if (the_image->ClearCurrentLayer(c) == TRUE) {
					Invalidate();
					AddChange();
				}
				release_sem(action_semaphore);
			}
			break;

		case B_COPY:
		case B_CUT:
			{
				int32 copied_layers;
				if (message->FindInt32("layers",&copied_layers) == B_OK) {
					DoCopyOrCut(copied_layers,message->what == B_CUT);
				}
				break;
			}

		case B_PASTE:
			DoPaste();
			break;

		case HS_UNDO:
			Undo();
			break;

		case HS_REDO:
			Redo();
			break;

		case HS_START_MANIPULATOR: {
			if (PostponeMessageAndFinishManipulator())
				break;

			if (message->FindInt32("manipulator_type", &manip_type) != B_OK)
				break;

			message->FindInt32("image_id", &add_on_id);
			try {
				ManipulatorServer* server = ManipulatorServer::Instance();
				if (!server)
					throw std::bad_alloc();

				fManipulator = server->ManipulatorFor((manipulator_type)manip_type,
					add_on_id);
				status_t err = message->FindInt32("layers",&manipulated_layers);
				if ((err == B_OK) && (fManipulator != NULL)) {
					GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
					ImageAdapter *adapter = cast_as(fManipulator,ImageAdapter);
					if (adapter != NULL)
						adapter->SetImage(the_image);

					SetCursor();
					// If the manipulator is not a GUIManipulator we put it to finish its
					// business. If the manipulator is actually a GUIManipulator we give the
					// correct preview-bitmap to the manipulator and set up its GUI.
					if (gui_manipulator == NULL) {
						start_thread(MANIPULATOR_FINISHER_THREAD);
					} else {
						// The order is important, first set the preview-bitmap
						// and only after that open the GUI for the manipulator.
						if (manipulated_layers == HS_MANIPULATE_CURRENT_LAYER) {
							gui_manipulator->SetPreviewBitmap(the_image->ReturnActiveBitmap());
						}
						else {
							gui_manipulator->SetPreviewBitmap(the_image->ReturnRenderedImage());
						}
						//StatusBarGUIManipulator *status_bar_gui_manipulator = cast_as(gui_manipulator,StatusBarGUIManipulator);
						WindowGUIManipulator *window_gui_manipulator = cast_as(gui_manipulator,WindowGUIManipulator);

						((PaintWindow*)Window())->SetHelpString(gui_manipulator->ReturnHelpString(),HS_TOOL_HELP_MESSAGE);
						if (window_gui_manipulator != NULL) {
							char window_name[256];
							sprintf(window_name, "%s: %s",
								ReturnProjectName(),
								window_gui_manipulator->ReturnName());

							BRect frame(100, 100, 200, 200);
							if (SettingsServer* server = SettingsServer::Instance()) {
								BMessage settings;
								server->GetApplicationSettings(&settings);
								settings.FindRect(skAddOnWindowFrame, &frame);
							}

							frame = FitRectToScreen(frame);
							manipulator_window = new ManipulatorWindow(frame,
								window_gui_manipulator->MakeConfigurationView(this),
									window_name, Window(), this);
						}

						cursor_mode = MANIPULATOR_CURSOR_MODE;
						SetCursor();

						// The manipulator might have updated the bitmap and
						// might also want to draw some GUI. We send a
						// HS_MANIPULATOR_ADJUSTING_FINISHED to this view
						// to get the manipulator update the correct bitmap.
						if (BWindow* window = Window())
							window->PostMessage(HS_MANIPULATOR_ADJUSTING_FINISHED, this);
					}
				}
			}
			catch (std::bad_alloc) {
				ShowAlert(CANNOT_START_MANIPULATOR_ALERT);
				delete fManipulator;
				fManipulator = NULL;
			}
		}	break;

		case HS_MANIPULATOR_FINISHED: {
			// First find whether the manipulator finished cancelling or OKing.
			bool finish_status = false;
			message->FindBool("status", &finish_status);
			if (GUIManipulator* guiManipulator =
				dynamic_cast<GUIManipulator*> (fManipulator)) {
				if (WindowGUIManipulator* windowGuiManipulator =
					dynamic_cast<WindowGUIManipulator*> (guiManipulator)) {
					(void)windowGuiManipulator;	// suppress warning
					if (manipulator_window) {
						manipulator_window->Lock();
						manipulator_window->Quit();
						manipulator_window = NULL;
					}
				}

				if (finish_status) {
					start_thread(MANIPULATOR_FINISHER_THREAD);
					AddChange();
				} else {
					// The manipulator should be instructed to restore
					// whatever changes it has made and should be quit then.
					guiManipulator->Reset(selection);
					((PaintWindow*)Window())->ReturnStatusView()->DisplayToolsAndColors();
					delete fManipulator;
					fManipulator = NULL;
					the_image->Render();
					manipulated_layers = HS_MANIPULATE_NO_LAYER;
					Invalidate();
				}
			}
			cursor_mode = NORMAL_CURSOR_MODE;
			SetCursor();
			// changes the help-string
			ToolManager::Instance().NotifyViewEvent(this, TOOL_ACTIVATED);
		}	break;

		case HS_MANIPULATOR_ADJUSTING_STARTED: {
		case HS_MANIPULATOR_ADJUSTING_FINISHED:
			continue_manipulator_updating = false;
			if (message->what == HS_MANIPULATOR_ADJUSTING_STARTED)
				continue_manipulator_updating = true;
			start_thread(MANIPULATOR_UPDATER_THREAD);
		}	break;

		default: {
			BView::MessageReceived(message);
		}	break;
	}
}

void ImageView::MouseDown(BPoint view_point)
{
	// From this function we call view-manipulator's MouseDown-function.
	// It will be the correct function depending on what manipulator is
	// in use currently.

	// here read the modifier keys
	BMessage *message = Window()->CurrentMessage();
	//int32 modifiers = message->FindInt32("modifiers");

	// here we read which mousebutton was pressed
	uint32 buttons = message->FindInt32("buttons");

	// The window activation should probably be made a user preference.
	// Also backgroundview should activate the window if the user wants it.
//	// activate the window if necessary
	if (Window()->IsActive() == FALSE)
		Window()->Activate(TRUE);

	// If the view is not focus view, make it the focus view
	if (!IsFocus())
		MakeFocus(true);

	// try to acquire the mouse_mutex
	if (acquire_sem_etc(mouse_mutex,1,B_TIMEOUT,0) == B_OK) {
		GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
		if (gui_manipulator != NULL) {
			start_thread(MANIPULATOR_MOUSE_THREAD);
		}
		else if (fManipulator == NULL){
			if (buttons & B_SECONDARY_MOUSE_BUTTON) {
				BMenuItem *item = ToolManager::Instance().ToolPopUpMenu()->Go(ConvertToScreen(view_point));
				if (item != NULL) {
					ToolManager::Instance().ChangeTool(item->Command());
					SetCursor();	// If the tool changes, the cursor should change too.
				}
				release_sem(mouse_mutex);
			}
			else if (buttons & B_TERTIARY_MOUSE_BUTTON) {
				BPoint point,prev_point;
				uint32 buttons;
				GetMouse(&point,&buttons);
				while (buttons) {
					prev_point = point;
					GetMouse(&point,&buttons);
					ScrollBy(point.x-prev_point.x,point.y-prev_point.y);
					snooze(25 * 1000);
				}
				release_sem(mouse_mutex);
			}
			else {
				start_thread(PAINTER_THREAD);
			}
		}
		else {
			release_sem(mouse_mutex);
		}
	}
	else {
		if (fManipulator == NULL) {
			BPoint bitmap_point;
			uint32 buttons;
			getCoords(&bitmap_point,&buttons,&view_point);
			int32 clicks;
			if (Window()->CurrentMessage()->FindInt32("clicks",&clicks) == B_OK)
				ToolManager::Instance().MouseDown(this,view_point,bitmap_point,buttons,clicks);
		}
	}
}

void ImageView::MouseMoved(BPoint where, uint32 transit, const BMessage *message)
{
	// here we will display the coordinates
	// we will also change the cursor whenever mouse enters the view or leaves it

	// If we have a message being dragged over us, do not change the cursor
	if (message == NULL) {
		if ((transit == B_ENTERED_VIEW) || (transit == B_EXITED_VIEW)) {
			if (fManipulator == NULL) {
				if (transit == B_ENTERED_VIEW)
					ToolManager::Instance().NotifyViewEvent(this,CURSOR_ENTERED_VIEW);
				else
					ToolManager::Instance().NotifyViewEvent(this,CURSOR_EXITED_VIEW);
			}
			SetCursor();
		}
	}
	else {
		if (message->what == HS_LAYER_DRAGGED) {
			BMessage help_view_message;

			if (transit == B_ENTERED_VIEW) {
				help_view_message.what = HS_TEMPORARY_HELP_MESSAGE;
				help_view_message.AddString("message","Drop layer to copy it to this image.");
				Window()->PostMessage(&help_view_message,Window());
			}
			else if (transit == B_EXITED_VIEW) {
				help_view_message.what = HS_TOOL_HELP_MESSAGE;
				Window()->PostMessage(&help_view_message,Window());
			}
		}
	}
	if (transit != B_EXITED_VIEW) {
		// change the point according to magnifying scale
		where.x = floor(where.x / getMagScale());
		where.y = floor(where.y / getMagScale());

		// round the point to grid units
		where = roundToGrid(where);

		// Here we set the window to display coordinates.
		((PaintWindow *)Window())->DisplayCoordinates(where,reference_point,use_reference_point);
	}
	else {
		where.x = the_image->Width();
		where.y = the_image->Height();

		((PaintWindow *)Window())->DisplayCoordinates(where,BPoint(0,0),false);
	}
}


bool
ImageView::Quit()
{
	 int32 mode = B_CONTROL_ON;
	if (SettingsServer* server = SettingsServer::Instance()) {
		BMessage settings;
		server->GetApplicationSettings(&settings);
		settings.FindInt32(skQuitConfirmMode, &mode);
	}

	if (mode == B_CONTROL_ON) {
		if (project_changed > 0) {
			char text[256];
			sprintf(text, StringServer::ReturnString(SAVE_CHANGES_STRING),
				project_name, project_changed);

			BAlert* alert = new BAlert("Unsaved Changes!", text,
				StringServer::ReturnString(CANCEL_STRING),
				StringServer::ReturnString(DO_NOT_SAVE_STRING),
				StringServer::ReturnString(SAVE_STRING), B_WIDTH_AS_USUAL,
				B_OFFSET_SPACING);

			int32 value = alert->Go();
			if (value == 0)		// Cancel
				return false;

			if (value == 1)		// Don't save
				return true;

			if (value == 2) {	// Save
				BMessage* message = new BMessage(HS_SAVE_PROJECT);
				message->SetInt32("TryAgain", 1);
				message->SetInt32("quitAll", ((PaintApplication*)be_app)->ShuttingDown());
				Window()->PostMessage(message, Window());
				return false;
			}

			return true;
		}
	}

	if (acquire_sem_etc(action_semaphore, 1, B_TIMEOUT, 0) != B_OK)
		return false;

	return true;
}


status_t ImageView::Freeze()
{
	if (acquire_sem(mouse_mutex) == B_OK) {
		if (acquire_sem(action_semaphore) == B_OK)
			return B_OK;
		release_sem(mouse_mutex);
	}

	// This must restore whatever the manipulator had previewed. Here we
	// also acquire mouse_mutex and action_semaphore semaphores to protect
	// the image from any further changes. These restorations must of course
	// be done only after we have acquired the desired semaphores.
	GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
	if (gui_manipulator != NULL) {
		gui_manipulator->Reset(selection);
		the_image->Render();
	}
	return B_ERROR;
}


status_t ImageView::UnFreeze()
{
	// Release the semaphores. We could also tell the manipulator to preview
	// with the settings that it last had, but we will not do it yet.
	release_sem(mouse_mutex);
	release_sem(action_semaphore);

	return B_OK;
}


void ImageView::getCoords(BPoint *bitmap_point, uint32 *buttons,BPoint *view_point)
{
	// this function converts the points using magnifying_scale
	// to correspond to real points in buffer
	// we also take any possible grids into account
	BPoint point;
	GetMouse(&point,buttons);
	(*bitmap_point).x = floor(point.x / getMagScale());
	(*bitmap_point).y = floor(point.y / getMagScale());
	// and finally we round the point to grid units
	(*bitmap_point) =	roundToGrid(*bitmap_point);

	// Also if the view_point is not NULL put the point into it
	if (view_point != NULL) {
		view_point->x = (*bitmap_point).x*getMagScale();
		view_point->y = (*bitmap_point).y*getMagScale();
	}
}


void ImageView::setGrid(BPoint origin, int32 unit)
{
	// here we give the grid-variables new values
	grid_unit = (unit >= 1 ? unit : 1);
	grid_origin = origin;
}


void ImageView::setMagScale(float scale)
{
	// this will be used to see if the scale was actually changed
	// and is there a need to draw the view
	float prev_scale = magnify_scale;

	if (scale <= HS_MAX_MAG_SCALE) {
		if (scale >= HS_MIN_MAG_SCALE) {
			magnify_scale = scale;
		}
		else
			magnify_scale = HS_MIN_MAG_SCALE;
	}
	else
		magnify_scale = HS_MAX_MAG_SCALE;

	selection->ChangeMagnifyingScale(magnify_scale);


	// if the scale was changed and the view is on screen draw the view
	// also update the scrollbars
	if ((prev_scale != magnify_scale) && !IsHidden()) {
		adjustSize();
		adjustPosition();
		adjustScrollBars();
		// we should here also draw the view
		Invalidate(Bounds());
	}

	((PaintWindow*)Window())->displayMag(magnify_scale);
}


BPoint ImageView::roundToGrid(BPoint point)
{
	// here we round the coordinates of point to nearest grid-unit
	// this version does not take the grid_origin into account
	point.x = point.x - (((int)point.x) % grid_unit);
	point.y = point.y - (((int)point.y) % grid_unit);

	return point;
}


BRect ImageView::convertBitmapRectToView(BRect rect)
{
	// bitmap might also have some offset that should be taken
	// into account

	// this only takes the mag_scale into account
	float scale = getMagScale();

	if (scale > 1.0) {
		rect.left = rect.left * scale;
		rect.top = rect.top * scale;
		rect.right = rect.right * scale + scale - 1;
		rect.bottom = rect.bottom * scale + scale - 1;
	}
	else if (scale < 1.0) {
		rect.left = floor(rect.left * scale);
		rect.top = floor(rect.top * scale);
		rect.right = floor(rect.right * scale);
		rect.bottom = floor(rect.bottom * scale);
	}


	// here convert the rect to use offset
	//rect.OffsetBy(((Layer*)layer_list->ItemAt(current_layer_index))->Offset().LeftTop());

	return rect;
}

BRect ImageView::convertViewRectToBitmap(BRect rect)
{
	// It is not possible to convert a view rectangle into
	// a bitmap rectangle perfectly if magnifying scale is
	// not 1. The rectangle will be converted to the smallest
	// possible rectangle. Half pixels will be converted so that the
	// rectangle is extended.
	float scale = getMagScale();

	BRect bitmap_rect;

	bitmap_rect.left = floor(rect.left / scale);
	bitmap_rect.top = floor(rect.top / scale);
	bitmap_rect.right = ceil(rect.right / scale);
	bitmap_rect.bottom = ceil(rect.bottom / scale);

	bitmap_rect = bitmap_rect & the_image->ReturnRenderedImage()->Bounds();
	return bitmap_rect;
}



void ImageView::ActiveLayerChanged()
{
	GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);

	if ((gui_manipulator != NULL) && (manipulated_layers == HS_MANIPULATE_CURRENT_LAYER)) {
		gui_manipulator->Reset(selection);
		gui_manipulator->SetPreviewBitmap(the_image->ReturnActiveBitmap());

		// We should also tell the manipulator to recalculate its preview.
		// This can be best achieved by sending a HS_MANIPULATOR_ADJUSTING_FINISHED
		// message to this view.
		Window()->PostMessage(HS_MANIPULATOR_ADJUSTING_FINISHED,this);
	}
}

void ImageView::adjustSize()
{
	if (LockLooper() == TRUE) {
		// resize the view to proper size
		BRect bg_bounds = Parent()->Bounds();
		BRect vertBounds = ScrollBar(B_VERTICAL)->Bounds();
		BRect horzBounds = ScrollBar(B_HORIZONTAL)->Bounds();
		ResizeTo(
			min_c(
				bg_bounds.Width() - vertBounds.Width() - 2,
				getMagScale() * the_image->Width() - 1),
			min_c(
				bg_bounds.Height() - horzBounds.Height() - 2,
				getMagScale() * the_image->Height() - 1));
		UnlockLooper();
	}
}

void ImageView::adjustPosition()
{
	if (LockLooper() == TRUE) {
		BPoint top_left;
		BRect bg_bounds = Parent()->Bounds();
		BRect vertBounds = ScrollBar(B_VERTICAL)->Bounds();
		BRect horzBounds = ScrollBar(B_HORIZONTAL)->Bounds();
		top_left.x = (bg_bounds.Width() - vertBounds.Width() -
			getMagScale()*the_image->Width()) / 2;
		top_left.y = (bg_bounds.Height() - horzBounds.Height() -
			getMagScale()*the_image->Height()) / 2;

		top_left.x = max_c(0,(int32)top_left.x);
		top_left.y = max_c(0,(int32)top_left.y);

		if (top_left != Frame().LeftTop())
			MoveTo(top_left);

		UnlockLooper();
	}
}


void ImageView::adjustScrollBars()
{
	if (LockLooper() == TRUE) {
		// set the horizontal bar
		if ((getMagScale()*the_image->Width() - (Frame().Width() + 1)) <= 0) {
			ScrollBar(B_HORIZONTAL)->SetRange(0,0);
			ScrollBar(B_HORIZONTAL)->SetProportion(1);
		}
		else {
			ScrollBar(B_HORIZONTAL)->SetRange(0,getMagScale()*the_image->Width() - (Frame().Width() + 1));
			ScrollBar(B_HORIZONTAL)->SetProportion((Frame().Width() + 1)/(getMagScale()*the_image->Width()));
		}

		// set the vertical bar
		if ((getMagScale()*the_image->Height() - (Frame().Height() + 1)) <= 0) {
			ScrollBar(B_VERTICAL)->SetRange(0,0);
			ScrollBar(B_VERTICAL)->SetProportion(1);
		}
		else {
			ScrollBar(B_VERTICAL)->SetRange(0,getMagScale()*the_image->Height() - (Frame().Height() + 1));
			ScrollBar(B_VERTICAL)->SetProportion((Frame().Height() + 1)/(getMagScale()*the_image->Height()));
		}

		UnlockLooper();
	}
}

Selection* ImageView::GetSelection()
{
	return selection;
}

void ImageView::start_thread(int32 thread_type)
{
	thread_id a_thread = spawn_thread(enter_thread,"ImageView thread",B_NORMAL_PRIORITY,this);
	if (a_thread >= 0) {
		resume_thread(a_thread);
		send_data(a_thread,thread_type,NULL,0);
	}
}

int32 ImageView::enter_thread(void *data)
{
	ImageView *this_pointer = (ImageView*)data;
	int32 thread_type = receive_data(NULL,NULL,0);
	int32 err = B_OK;

	if (thread_type == MANIPULATOR_FINISHER_THREAD) {
		acquire_sem(this_pointer->action_semaphore);
	}
	else if (acquire_sem_etc(this_pointer->action_semaphore,1,B_TIMEOUT,0) != B_OK) {
		if ((thread_type == PAINTER_THREAD) || (thread_type == MANIPULATOR_MOUSE_THREAD))
			release_sem(this_pointer->mouse_mutex);
		return B_ERROR;
	}

	if (this_pointer) {
		switch (thread_type) {
			case PAINTER_THREAD:
				err = this_pointer->PaintToolThread();
				break;
			case MANIPULATOR_MOUSE_THREAD:
				err = this_pointer->ManipulatorMouseTrackerThread();
				break;
			case MANIPULATOR_UPDATER_THREAD:
				err = this_pointer->GUIManipulatorUpdaterThread();
				break;
			case MANIPULATOR_FINISHER_THREAD:
				err = this_pointer->ManipulatorFinisherThread();
				break;
			default:
				break;
		}
	}

	// When the work has been done we may once again permit
	// the reading of the mouse.
	if ((thread_type == PAINTER_THREAD) || (thread_type == MANIPULATOR_MOUSE_THREAD))
		release_sem(this_pointer->mouse_mutex);

	release_sem(this_pointer->action_semaphore);

	this_pointer->SetReferencePoint(BPoint(0,0),FALSE);

	return err;
}


int32 ImageView::PaintToolThread()
{
	uint32 buttons;
	BPoint point;
	BPoint view_point;
	if (LockLooper() == TRUE) {
		getCoords(&point,&buttons,&view_point);
		UnlockLooper();
	}

	SetReferencePoint(point,TRUE);

	int32 tool_type = ToolManager::Instance().ReturnActiveToolType();

	if (modifiers() & B_COMMAND_KEY) {
		tool_type = COLOR_SELECTOR_TOOL;
	}

	if (tool_type != TEXT_TOOL) {
		if (tool_type != NO_TOOL) {
			// When this function returns the tool has finished. This function
			// might not return even if the user releases the mouse-button (in
			// which case for example a double-click might make it return).
			ToolScript* script = ToolManager::Instance().StartTool(this, buttons, point,
				view_point, tool_type);
			if (script && tool_type != SELECTOR_TOOL) {
				const DrawingTool* tool = ToolManager::Instance().ReturnTool(tool_type);
				UndoEvent *new_event = undo_queue->AddUndoEvent(tool->Name(),
					the_image->ReturnThumbnailImage());
				BList *layer_list = the_image->LayerList();
				if (new_event != NULL) {
					for (int32 i=0;i<layer_list->CountItems();i++) {
						Layer *layer = (Layer*)layer_list->ItemAt(i);
						UndoAction *new_action;
						if (layer->IsActive() == FALSE)
							new_action = new UndoAction(layer->Id());
						else
							new_action = new UndoAction(layer->Id(), script,ToolManager::Instance().LastUpdatedRect(this));

						new_event->AddAction(new_action);
						new_action->StoreUndo(layer->Bitmap());
					}
					if ((new_event != NULL) && (new_event->IsEmpty() == TRUE)) {
						undo_queue->RemoveEvent(new_event);
						delete new_event;
					}
					AddChange();
				}
				else {
					delete script;
					ToolManager::Instance().LastUpdatedRect(this);
				}
			}
			else if (tool_type == SELECTOR_TOOL) {
				// Add selection-change to the undo-queue.
				if (!(*undo_queue->ReturnSelectionData() == *selection->ReturnSelectionData())) {
					const DrawingTool *used_tool = ToolManager::Instance().ReturnTool(tool_type);
					UndoEvent *new_event = undo_queue->AddUndoEvent(used_tool->Name(),the_image->ReturnThumbnailImage());
					if (new_event != NULL) {
						new_event->SetSelectionData(undo_queue->ReturnSelectionData());
						undo_queue->SetSelectionData(selection->ReturnSelectionData());
					}
				}

				// Tell the selection to start drawing itself.
				selection->StartDrawing(this,magnify_scale);
			}
			return B_OK;
		}
	} else {
		if (ManipulatorServer* server = ManipulatorServer::Instance()) {
			if (!fManipulator) {
				if (TextManipulator* manipulator = dynamic_cast<TextManipulator*>
					(server->ManipulatorFor(TEXT_MANIPULATOR))) {
					manipulator->SetStartingPoint(point);
					manipulated_layers = HS_MANIPULATE_CURRENT_LAYER;
					manipulator->SetPreviewBitmap(the_image->ReturnActiveBitmap());

					PaintWindow* window = dynamic_cast<PaintWindow*> (Window());
					if (window && LockLooper()) {
						window->SetHelpString(manipulator->ReturnHelpString(),
							HS_TOOL_HELP_MESSAGE);
						UnlockLooper();
					}

					BString name = ReturnProjectName();
					name << ": " << manipulator->ReturnName();

					manipulator_window = new ManipulatorWindow(BRect(100, 100,
						200.0, 200.0), manipulator->MakeConfigurationView(this),
						name.String(), window, this);

					if (window) {
						window->PostMessage(HS_MANIPULATOR_ADJUSTING_FINISHED,
							this);
					}

					fManipulator = manipulator;
					cursor_mode = MANIPULATOR_CURSOR_MODE;

					SetCursor();
					return B_OK;
				}
			}
		}
	}
	return B_ERROR;
}


int32 ImageView::ManipulatorMouseTrackerThread()
{
	GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
	if (gui_manipulator == NULL)
		return B_ERROR;


	BPoint point;
	uint32 buttons;
	if (LockLooper() == TRUE) {
		getCoords(&point,&buttons);
		UnlockLooper();
	}
	int32 preview_quality;
	bool first_call_to_mouse_down = TRUE;
	BRegion *updated_region = new BRegion();

	float number_of_frames = 0;
	float time = system_time();
	while (buttons) {
		updated_region->MakeEmpty();
		if (LockLooper() == TRUE) {
			gui_manipulator->MouseDown(point,buttons,this,first_call_to_mouse_down);
			first_call_to_mouse_down = FALSE;
			preview_quality = gui_manipulator->PreviewBitmap(selection,FALSE,updated_region);
			if (preview_quality != DRAW_NOTHING) {
				if ((preview_quality != DRAW_ONLY_GUI) && (updated_region->Frame().IsValid())) {
					if (manipulated_layers != HS_MANIPULATE_ALL_LAYERS) {
						the_image->RenderPreview(updated_region->Frame(),preview_quality);
					}
					else {
						the_image->MultiplyRenderedImagePixels(preview_quality);
					}
					for (int32 i=0;i<updated_region->CountRects();i++)
						Draw(convertBitmapRectToView(updated_region->RectAt(i)));
				}
				else if (preview_quality == DRAW_ONLY_GUI) {
					DrawManipulatorGUI(TRUE);
					selection->Draw();
					Flush();
				}
				number_of_frames++;
			}
			else {
				snooze(50 * 1000);
			}
			getCoords(&point,&buttons);
			UnlockLooper();
		}
	}

	time = system_time() - time;
	time = time / 1000000.0;

//	printf("Frames per second: %f\n",number_of_frames/time);

	cursor_mode = BLOCKING_CURSOR_MODE;
	SetCursor();

	updated_region->Set(BRect(0,0,-1,-1));
	preview_quality = gui_manipulator->PreviewBitmap(selection,TRUE,updated_region);
	if (preview_quality != DRAW_NOTHING) {
		if ((preview_quality != DRAW_ONLY_GUI) && (updated_region->Frame().IsValid())) {
			if (manipulated_layers != HS_MANIPULATE_ALL_LAYERS) {
				the_image->RenderPreview(updated_region->Frame(),preview_quality);
			}
			else
				the_image->MultiplyRenderedImagePixels(preview_quality);

			if (LockLooper() == TRUE) {
				for (int32 i=0;i<updated_region->CountRects();i++)
					Draw(convertBitmapRectToView(updated_region->RectAt(i)));
				UnlockLooper();
			}
		}
		else if (preview_quality == DRAW_ONLY_GUI) {
			if (LockLooper() == TRUE) {
				DrawManipulatorGUI(TRUE);
				selection->Draw();
				Flush();
				UnlockLooper();
			}
		}
	}

	cursor_mode = MANIPULATOR_CURSOR_MODE;
	SetCursor();

	delete updated_region;
	return B_OK;
}


int32 ImageView::GUIManipulatorUpdaterThread()
{
	GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
	if (gui_manipulator == NULL)
		return B_ERROR;


	int32 preview_quality = 0;
	int32 lowest_quality = 0;
	BRegion *updated_region = new BRegion();

	float number_of_frames = 0;
	float time = system_time();
	while (continue_manipulator_updating) {
		if (LockLooper() == TRUE) {
			preview_quality = gui_manipulator->PreviewBitmap(selection,FALSE,updated_region);
			lowest_quality = max_c(lowest_quality,preview_quality);

			if ((preview_quality != DRAW_NOTHING) && (updated_region->Frame().IsValid())) {
				if (preview_quality != DRAW_ONLY_GUI) {
					if (manipulated_layers != HS_MANIPULATE_ALL_LAYERS) {
						the_image->RenderPreview(updated_region->Frame(),preview_quality);
					}
					else
						the_image->MultiplyRenderedImagePixels(preview_quality);

					for (int32 i=0;i<updated_region->CountRects();i++)
						Draw(convertBitmapRectToView(updated_region->RectAt(i)));
				}
				else if (preview_quality == DRAW_ONLY_GUI) {
					DrawManipulatorGUI(TRUE);
					selection->Draw();
					Flush();
				}
				number_of_frames++;
			}
			else {
				snooze(50 * 1000);
			}
			UnlockLooper();
		}
		if (preview_quality == lowest_quality) {
			snooze(50 * 1000);
		}
		else {
			snooze(20 * 1000);
		}
	}

	time = system_time() - time;
	time = time / 1000000.0;

//	if (time > 0)
//		printf("Frames per second: %f\n",number_of_frames/time);


	updated_region->Set(BRect(0,0,-1,-1));

	cursor_mode = BLOCKING_CURSOR_MODE;
	SetCursor();

	preview_quality = gui_manipulator->PreviewBitmap(selection,TRUE,updated_region);


	if (preview_quality != DRAW_NOTHING) {
		if ((preview_quality != DRAW_ONLY_GUI) && (updated_region->Frame().IsValid())) {
			if (manipulated_layers != HS_MANIPULATE_ALL_LAYERS) {
				the_image->RenderPreview(updated_region->Frame(),preview_quality);
			}
			else
				the_image->MultiplyRenderedImagePixels(preview_quality);

			if (LockLooper() == TRUE) {
				for (int32 i=0;i<updated_region->CountRects();i++)
					Draw(convertBitmapRectToView(updated_region->RectAt(i)));
				UnlockLooper();
			}
		}
		else if (preview_quality == DRAW_ONLY_GUI){
			if (LockLooper() == TRUE) {
				DrawManipulatorGUI(TRUE);
				selection->Draw();
				Flush();
				UnlockLooper();
			}
		}
	}

	cursor_mode = MANIPULATOR_CURSOR_MODE;
	SetCursor();

	delete updated_region;
	return B_OK;
}

int32 ImageView::ManipulatorFinisherThread()
{
	if (fManipulator == NULL) {
		if (manipulator_finishing_message != NULL) {
			if (LockLooper() == TRUE) {
				Window()->PostMessage(manipulator_finishing_message,this);
				delete manipulator_finishing_message;
				manipulator_finishing_message = NULL;
				UnlockLooper();
			}
		}
		return B_ERROR;
	}
	cursor_mode = BLOCKING_CURSOR_MODE;
	SetCursor();

	// Here we should set up the status-bar
	BStatusBar *status_bar = ((PaintWindow*)Window())->ReturnStatusView()->DisplayProgressIndicator();
	if (status_bar != NULL) {
		if (LockLooper() == TRUE) {
			status_bar->Reset();
			status_bar->SetText(StringServer::ReturnString(FINISHING_STRING));
			UnlockLooper();
		}
	}
	GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
	UndoEvent *new_event = NULL;

	try {
		if (manipulated_layers == HS_MANIPULATE_CURRENT_LAYER) {
			BBitmap *buffer = the_image->ReturnActiveBitmap();
			BBitmap *new_buffer = NULL;
			if (gui_manipulator != NULL) {
				ManipulatorSettings *settings = gui_manipulator->ReturnSettings();
				new_buffer = gui_manipulator->ManipulateBitmap(settings,buffer,selection,status_bar);
				delete settings;
			}
			else {
				new_buffer = fManipulator->ManipulateBitmap(buffer,selection,status_bar);
			}

			Layer *the_layer = the_image->ReturnActiveLayer();
			if (new_buffer && new_buffer != buffer)
				the_layer->ChangeBitmap(new_buffer);

			new_event = undo_queue->AddUndoEvent(fManipulator->ReturnName(),
				the_image->ReturnThumbnailImage());
			if (new_event != NULL) {
				BList *layer_list = the_image->LayerList();
				for (int32 i=0;i<layer_list->CountItems();i++) {
					Layer *layer = (Layer*)layer_list->ItemAt(i);

					UndoAction *new_action;
					if ((layer != the_layer) || (new_buffer == NULL))
						new_action = new UndoAction(layer->Id());
					else {
						BRegion affected_region(new_buffer->Bounds());
						new_action = new UndoAction(layer->Id(),
							fManipulator->ReturnSettings(),
							new_buffer->Bounds(),
							(manipulator_type)manip_type, add_on_id);
					}
					new_event->AddAction(new_action);
					new_action->StoreUndo(layer->Bitmap());
				}
			}
		}
		else {
			// Do all the layers.
			// Here we record the dimensions of all the bitmaps. If the
			// dimensions change the composite bitmap should be changed to
			// smallest layer dimension.

			BList* layer_list = the_image->LayerList();
			int32 layerCount = layer_list->CountItems();
			if (status_bar != NULL)
				status_bar->SetMaxValue(layerCount * 100);

			new_event = undo_queue->AddUndoEvent(fManipulator->ReturnName(),
				the_image->ReturnThumbnailImage());

			for (int32 i = 0; i < layerCount; ++i) {
				if (LockLooper()) {
					char text[256];
					sprintf(text,"%s %ld / %ld",
						StringServer::ReturnString(LAYER_STRING), i + 1, layerCount);
					status_bar->SetTrailingText(text);
					UnlockLooper();
				}

				Layer* the_layer = static_cast<Layer*> (layer_list->ItemAt(i));
				BBitmap *buffer = the_layer->Bitmap();
				BBitmap *new_buffer;
				if (gui_manipulator != NULL) {
					ManipulatorSettings *settings = gui_manipulator->ReturnSettings();
					new_buffer = gui_manipulator->ManipulateBitmap(settings,buffer,selection,status_bar);
					delete settings;
				}
				else {
					new_buffer = fManipulator->ManipulateBitmap(buffer,selection,status_bar);
				}

				if (new_buffer && new_buffer != buffer)
					the_layer->ChangeBitmap(new_buffer);

				if (new_event != NULL) {
					if (new_buffer != NULL) {
						BRegion affected_region(new_buffer->Bounds());
						UndoAction *new_action;
						new_action = new UndoAction(the_layer->Id(),fManipulator->ReturnSettings(),new_buffer->Bounds(),(manipulator_type)manip_type,add_on_id);

						new_event->AddAction(new_action);
						new_action->StoreUndo(the_layer->Bitmap());
						thread_id a_thread = spawn_thread(Layer::CreateMiniatureImage,"create mini picture",B_LOW_PRIORITY,the_layer);
						resume_thread(a_thread);
					}
					else {
						new_event->AddAction(new UndoAction(the_layer->Id()));
					}
				}
			}
		}
	}
	catch (std::bad_alloc e) {
		ShowAlert(CANNOT_FINISH_MANIPULATOR_ALERT);
		// The manipulator should be asked to reset the preview-bitmap, if it is
		// a GUIManipulator.
		if (gui_manipulator != NULL) {
			gui_manipulator->Reset(selection);
		}
	}

	manipulated_layers = HS_MANIPULATE_NO_LAYER;


	// If the added UndoEvent is empty then destroy it.
	if ((new_event != NULL) && (new_event->IsEmpty() == TRUE)) {
		undo_queue->RemoveEvent(new_event);
		delete new_event;
	}

	the_image->SetImageSize();
	the_image->Render();

	// also recalculate the selection
	selection->Recalculate();

	// Change the selection for the undo-queue if necessary.
	if ((new_event != NULL) && !(*undo_queue->ReturnSelectionData() == *selection->ReturnSelectionData())) {
		new_event->SetSelectionData(undo_queue->ReturnSelectionData());
		undo_queue->SetSelectionData(selection->ReturnSelectionData());
	}

	// Store the manipulator settings.
	if (ManipulatorServer* server = ManipulatorServer::Instance())
		server->StoreManipulatorSettings(fManipulator);


	// Finally return the window to normal state and redisplay it.
	if (LockLooper() == true) {
		((PaintWindow*)Window())->ReturnStatusView()->DisplayToolsAndColors();

		Invalidate();
		UnlockLooper();
	}

	// The manipulator has finished, now finish the manipulator
	delete fManipulator;
	fManipulator = NULL;

	cursor_mode = NORMAL_CURSOR_MODE;
	SetCursor();

	if (manipulator_finishing_message != NULL) {
		if (LockLooper() == TRUE) {
			Window()->PostMessage(manipulator_finishing_message,this);
			delete manipulator_finishing_message;
			manipulator_finishing_message = NULL;
			UnlockLooper();
		}
	}


	return B_OK;
}




void ImageView::Undo()
{
	if (acquire_sem_etc(action_semaphore,1,B_TIMEOUT,0) == B_OK) {
		// If there is a GUI-manipulator, it should reset the bitmap before undo can be done.
		cursor_mode = BLOCKING_CURSOR_MODE;
		SetCursor();

		GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
		if (gui_manipulator != NULL) {
			gui_manipulator->Reset(selection);
		}

		UndoEvent *event = undo_queue->Undo();
		if (event != NULL) {
			if (event->IsEmpty() == FALSE) {
				the_image->UpdateImageStructure(event);

				cursor_mode = NORMAL_CURSOR_MODE;
				// After the undo, the current buffer might have changed and the
				// possible gui_manipulator should be informed about it.
				if (gui_manipulator != NULL) {
					gui_manipulator->SetPreviewBitmap(the_image->ReturnActiveBitmap());
					// We should also tell the manipulator to recalculate its preview.
					Window()->PostMessage(HS_MANIPULATOR_ADJUSTING_FINISHED,this);
					cursor_mode = MANIPULATOR_CURSOR_MODE;
				}
				LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
				AddChange();	// Removing a change here is not a good thing
			}

			if (event->ReturnSelectionData() != NULL) {
				SelectionData *data = new SelectionData(event->ReturnSelectionData());
				event->SetSelectionData(selection->ReturnSelectionData());
				selection->SetSelectionData(data);

				undo_queue->SetSelectionData(data);
				delete data;
			}

			Invalidate();
		}

		if (gui_manipulator != NULL)
			cursor_mode = MANIPULATOR_CURSOR_MODE;
		else
			cursor_mode = NORMAL_CURSOR_MODE;

		SetCursor();

		release_sem(action_semaphore);
	}
}



void ImageView::Redo()
{
	if (acquire_sem_etc(action_semaphore,1,B_TIMEOUT,0) == B_OK) {
		// If there is a GUI-manipulator, it should reset the bitmap before redo
		// can be done.
		GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
		if (gui_manipulator != NULL) {
			gui_manipulator->Reset(selection);
		}

		UndoEvent *event = undo_queue->Redo();
		if (event != NULL) {
			if (event->IsEmpty() == FALSE) {
				the_image->UpdateImageStructure(event);

				// After the redo, the current buffer might have changed and the
				// possible gui_manipulator should be informed about it.
				if (gui_manipulator != NULL) {
					gui_manipulator->SetPreviewBitmap(the_image->ReturnActiveBitmap());
					// We should also tell the manipulator to recalculate its preview.
					Window()->PostMessage(HS_MANIPULATOR_ADJUSTING_FINISHED,this);
				}

				LayerWindow::ActiveWindowChanged(Window(),the_image->LayerList(),the_image->ReturnThumbnailImage());
				AddChange();
			}
			if (event->ReturnSelectionData() != NULL) {
				SelectionData *data = new SelectionData(event->ReturnSelectionData());
				event->SetSelectionData(selection->ReturnSelectionData());
				selection->SetSelectionData(data);

				undo_queue->SetSelectionData(data);
				delete data;
			}

			Invalidate();
		}

		release_sem(action_semaphore);
	}
}

status_t
ImageView::DoCopyOrCut(int32 layers,bool cut)
{
	if (acquire_sem_etc(action_semaphore,1,B_TIMEOUT,0) == B_OK) {
		BBitmap *buffer;
		BRect* offset;
		bool ok_to_archive = TRUE;
		if (layers == HS_MANIPULATE_CURRENT_LAYER)
			buffer = the_image->ReturnActiveBitmap();
		else
			buffer = the_image->ReturnRenderedImage();
		BMessage *bitmap_archive = new BMessage();
		if (selection->IsEmpty() == TRUE) {
			if (buffer->Archive(bitmap_archive) != B_OK)
				ok_to_archive = FALSE;
		}
		else {
			BRect selection_bounds = selection->GetBoundingRect();
			selection_bounds.left = floor(selection_bounds.left);
			selection_bounds.top = floor(selection_bounds.top);
			selection_bounds.right = ceil(selection_bounds.right);
			selection_bounds.bottom = ceil(selection_bounds.bottom);
			selection_bounds = selection_bounds & buffer->Bounds();
			BRect bounds = selection_bounds;

			bounds.OffsetTo(0,0);
			BBitmap *to_be_archived = new BBitmap(bounds,B_RGBA32,0); //stargater, Pete
			uint32 *target_bits = (uint32*)to_be_archived->Bits();
			int32 bits_length = to_be_archived->BitsLength()/4;
			union {
				uint8 bytes[4];
				uint32 word;
			} color;
			color.bytes[0] = 0xFF;
			color.bytes[1] = 0xFF;
			color.bytes[2] = 0xFF;
			color.bytes[3] = 0x00;


			for (int32 i=0;i<bits_length;i++) {
				*target_bits++ = color.word;
			}
			target_bits = (uint32*)to_be_archived->Bits();
			int32 left = (int32)selection_bounds.left;
			int32 right = (int32)selection_bounds.right;
			int32 top = (int32)selection_bounds.top;
			int32 bottom = (int32)selection_bounds.bottom;

			uint32 *source_bits = (uint32*)buffer->Bits();
			int32 source_bpr = buffer->BytesPerRow()/4;

			for (int32 y=top;y<=bottom;y++) {
				for (int32 x=left;x<=right;x++) {
					if (selection->ContainsPoint(x,y))
						*target_bits = *(source_bits + x + y*source_bpr);

					target_bits++;
				}
			}
			if (to_be_archived->Archive(bitmap_archive) != B_OK)
				ok_to_archive = FALSE;

			delete to_be_archived;
		}

		if (ok_to_archive == TRUE) {
			be_clipboard->Lock();
			be_clipboard->Clear();
			BMessage *clipboard_message = be_clipboard->Data();
			clipboard_message->AddMessage("image/bitmap",bitmap_archive);
			if (selection->IsEmpty() == FALSE)
				clipboard_message->AddRect("offset", selection->GetBoundingRect());
			be_clipboard->Commit();
			be_clipboard->Unlock();
			delete bitmap_archive;
		}
		if (cut == TRUE) {
			if (layers == HS_MANIPULATE_CURRENT_LAYER)
				Window()->PostMessage(HS_CLEAR_LAYER,this);
			else
				Window()->PostMessage(HS_CLEAR_CANVAS,this);
		}

		release_sem(action_semaphore);
		return B_OK;
	}
	else
		return B_ERROR;
}


status_t
ImageView::DoPaste()
{
	be_clipboard->Lock();
	BMessage *bitmap_message = new BMessage();
	BMessage *clipboard_message = be_clipboard->Data();
	if (clipboard_message != NULL) {
		if (clipboard_message->FindMessage("image/bitmap",bitmap_message) == B_OK) {
			if (bitmap_message != NULL) {
				BBitmap *pasted_bitmap = new BBitmap(bitmap_message);
				BRect offset;
				clipboard_message->FindRect("offset", &offset);
				delete bitmap_message;
				if ((pasted_bitmap != NULL) && (pasted_bitmap->IsValid() == TRUE)) {
					try {
						if (the_image->AddLayer(pasted_bitmap, NULL, TRUE,
							1.0, &offset) != NULL) {
						//	delete pasted_bitmap;
							Invalidate();
							LayerWindow::ActiveWindowChanged(Window(),
								the_image->LayerList(),
								the_image->ReturnThumbnailImage());
							AddChange();
						}
					}
					catch (std::bad_alloc e) {
						ShowAlert(CANNOT_ADD_LAYER_ALERT);
					}
				}
			}
		}
	}
	be_clipboard->Unlock();
	return B_OK;
}



status_t ImageView::ShowAlert(int32 alert)
{
	const char *text;
	switch (alert) {
		case CANNOT_ADD_LAYER_ALERT:
			text = StringServer::ReturnString(MEMORY_ALERT_1_STRING);
			break;
		case CANNOT_START_MANIPULATOR_ALERT:
			text = StringServer::ReturnString(MEMORY_ALERT_2_STRING);
			break;

		case CANNOT_FINISH_MANIPULATOR_ALERT:
			text = StringServer::ReturnString(MEMORY_ALERT_3_STRING);
			break;

		default:
			text = "This alert should never show up";
			break;
	}

	BAlert *alert_box = new BAlert("alert_box",text,StringServer::ReturnString(OK_STRING),NULL,NULL,B_WIDTH_AS_USUAL,B_WARNING_ALERT);
	alert_box->Go();


	return B_OK;
}


void ImageView::SetCursor()
{
	BPoint point;
	uint32 buttons;
	BRegion region;

	if (LockLooper() == TRUE) {
		GetClippingRegion(&region);
		GetMouse(&point,&buttons);
		UnlockLooper();
	}

	if (region.Contains(point)) {
		if (cursor_mode == NORMAL_CURSOR_MODE) {
			be_app->SetCursor(ToolManager::Instance().ReturnCursor());
		}
		else if (cursor_mode == MANIPULATOR_CURSOR_MODE) {
			GUIManipulator *gui_manipulator = cast_as(fManipulator,GUIManipulator);
			if (gui_manipulator != NULL) {
				if (gui_manipulator->ManipulatorCursor() != NULL)
					be_app->SetCursor(gui_manipulator->ManipulatorCursor());
				else
					be_app->SetCursor(B_HAND_CURSOR);
			}
		}
		else if (cursor_mode == BLOCKING_CURSOR_MODE) {
			be_app->SetCursor(HS_BLOCKING_CURSOR);
		}
	}
	else {
		be_app->SetCursor(B_HAND_CURSOR);
	}
}


void ImageView::SetDisplayMode(int32 new_display_mode)
{
	if (current_display_mode != new_display_mode) {
		current_display_mode = new_display_mode;

		if (current_display_mode == DITHERED_8_BIT_DISPLAY_MODE) {
			if (the_image->RegisterDitheredUser(this) == B_ERROR) {
				current_display_mode = FULL_RGB_DISPLAY_MODE;
			}
		}
		else {
			the_image->UnregisterDitheredUser(this);
		}

		Invalidate();
	}
}


void ImageView::SetReferencePoint(BPoint point,bool use)
{
	reference_point = point;
	use_reference_point = use;
}

void ImageView::SetToolHelpString(const char *string)
{
	if (LockLooper() == TRUE) {
		((PaintWindow*)Window())->SetHelpString(string,HS_TOOL_HELP_MESSAGE);
		UnlockLooper();
	}
}


void ImageView::SetProjectName(const char *name)
{
	delete[] project_name;

	project_name = new char[strlen(name) + 1];
	strcpy(project_name,name);

	setWindowTitle();
}


void ImageView::SetImageName(const char *name)
{
	delete[] image_name;

	image_name = new char[strlen(name) + 1];
	strcpy(image_name,name);

	setWindowTitle();
}



void
ImageView::setWindowTitle()
{
	BString title;
	if (true) {
		// Experimental style title
		BString pname = project_name;
		BString iname = image_name;

		BString pchanged = "";
		BString ichanged = "";

		if (project_name == NULL)
			pname = "";

		if (project_changed > 0)
			pchanged = "(*) ";
		if (image_changed > 0)
			ichanged = "(*)";

		title << pchanged << pname;
		if (iname != NULL)
			title << " | (" << iname << ")";
	}

	if (LockLooper()) {
		Window()->SetTitle(title);
		UnlockLooper();
	}
}


void ImageView::AddChange()
{
	project_changed++;
	image_changed++;

	if ((project_changed == 1) || (image_changed == 1)) {
		setWindowTitle();
	}
}


void ImageView::RemoveChange()
{
	project_changed = max_c(0,project_changed-1);
	image_changed = max_c(0,image_changed-1);
}


void ImageView::ResetChangeStatistics(bool project, bool image)
{
	if (project)
		project_changed = 0;
	if (image)
		image_changed = 0;

	setWindowTitle();
}


bool ImageView::PostponeMessageAndFinishManipulator()
{
	if (fManipulator) {
		BMessage message(HS_MANIPULATOR_FINISHED);
		message.AddBool("status", true);
		Window()->PostMessage(&message, this);

		manipulator_finishing_message = Window()->DetachCurrentMessage();
		return true;
	}
	return false;
}







filter_result KeyFilterFunction(BMessage *message,BHandler **handler,BMessageFilter*)
{
//	message->PrintToStream();
	ImageView *view = dynamic_cast<ImageView*>(*handler);
	if (view != NULL) {
		if (acquire_sem_etc(view->mouse_mutex,1,B_RELATIVE_TIMEOUT,0) == B_OK) {
			const char *bytes;
			if ((!(modifiers() & B_COMMAND_KEY)) && (!(modifiers() & B_CONTROL_KEY))) {
				if (message->FindString("bytes",&bytes) == B_OK) {
					switch (bytes[0]) {
						case 'b':
							ToolManager::Instance().ChangeTool(BRUSH_TOOL);
							view->SetCursor();
							break;

						case 'a':
							ToolManager::Instance().ChangeTool(AIR_BRUSH_TOOL);
							view->SetCursor();
							break;

						case 'e':
							ToolManager::Instance().ChangeTool(ERASER_TOOL);
							view->SetCursor();
							break;

						case 'f':
							ToolManager::Instance().ChangeTool(FREE_LINE_TOOL);
							view->SetCursor();
							break;

						case 's':
							ToolManager::Instance().ChangeTool(SELECTOR_TOOL);
							view->SetCursor();
							break;

						case 'r':
							ToolManager::Instance().ChangeTool(RECTANGLE_TOOL);
							view->SetCursor();
							break;

						case 'l':
							ToolManager::Instance().ChangeTool(STRAIGHT_LINE_TOOL);
							view->SetCursor();
							break;

						case 'h':
							ToolManager::Instance().ChangeTool(HAIRY_BRUSH_TOOL);
							view->SetCursor();
							break;

						case 'u':
							ToolManager::Instance().ChangeTool(BLUR_TOOL);
							view->SetCursor();
							break;

						case 'i':
							ToolManager::Instance().ChangeTool(FILL_TOOL);
							view->SetCursor();
							break;

						case 't':
							ToolManager::Instance().ChangeTool(TEXT_TOOL);
							view->SetCursor();
							break;

						case 'n':
							ToolManager::Instance().ChangeTool(TRANSPARENCY_TOOL);
							view->SetCursor();
							break;

						case 'c':
							ToolManager::Instance().ChangeTool(COLOR_SELECTOR_TOOL);
							view->SetCursor();
							break;

						case 'p':
							ToolManager::Instance().ChangeTool(ELLIPSE_TOOL);
							view->SetCursor();
							break;
					}
				}
			}

			release_sem(view->mouse_mutex);
		}
	}
	return B_DISPATCH_MESSAGE;
}
