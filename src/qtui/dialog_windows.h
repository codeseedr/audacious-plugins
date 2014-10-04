/*
 * dialog_windows.h
 * Copyright 2014 John Lindgren and Michał Lipski
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#ifndef DIALOG_WINDOWS_H
#define DIALOG_WINDOWS_H

class QMessageBox;
class QWidget;

class DialogWindows
{
public:
    DialogWindows (QWidget * parent);
    ~DialogWindows ();

private:
    QWidget * m_parent;
    QMessageBox * m_progress = nullptr;
    QMessageBox * m_error = nullptr;

    void create_progress ();
    void create_error (const char * message);

    static void show_progress (void * message, void * data);
    static void show_progress_2 (void * message, void * data);
    static void hide_progress (void *, void * data);
    static void show_error (void * message, void * data);
};

#endif // DIALOG_WINDOWS_H
