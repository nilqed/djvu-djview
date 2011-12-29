//C-  -*- C++ -*-
//C- -------------------------------------------------------------------
//C- DjView4
//C- Copyright (c) 2006-  Leon Bottou
//C-
//C- This software is subject to, and may be distributed under, the
//C- GNU General Public License, either version 2 of the license,
//C- or (at your option) any later version. The license should have
//C- accompanied the software or you may obtain a copy of the license
//C- from the Free Software Foundation at http://www.fsf.org .
//C-
//C- This program is distributed in the hope that it will be useful,
//C- but WITHOUT ANY WARRANTY; without even the implied warranty of
//C- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//C- GNU General Public License for more details.
//C-  ------------------------------------------------------------------

#if AUTOCONF
# include "config.h"
#endif

#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>
#include <locale.h>
#include <stdio.h>

#include "qdjvu.h"
#include "qdjview.h"
#include "qdjvuwidget.h"
#include "djview.h"
#ifdef Q_WS_X11
# ifndef NPDJVU
#  include "qdjviewplugin.h"
# endif
#endif

#include <QtGlobal>
#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileOpenEvent>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegExp>
#include <QSettings>
#include <QSessionManager>
#include <QString>
#include <QStringList>
#include <QTranslator>


#ifdef QT_NO_DEBUG
static bool verbose = false;
#else
static bool verbose = true;
#endif


static void 
qtMessageHandler(QtMsgType type, const char *msg)
{
  switch (type) 
    {
    case QtFatalMsg:
      fprintf(stderr,"djview fatal error: %s\n", msg);
      abort();
    case QtCriticalMsg:
      fprintf(stderr,"djview critical error: %s\n", msg);
      break;
    case QtWarningMsg:
      if (verbose)
        fprintf(stderr,"djview: %s\n", msg);
      break;
    default:
      if (verbose)
        fprintf(stderr,"%s\n", msg);
      break;
    }
}


static void
message(QString string, bool prefix=true)
{
  QByteArray m = string.toLocal8Bit();
  if (prefix)
    fprintf(stderr, "djview: ");
  fprintf(stderr, "%s\n", (const char*) m);
}

static void
message(QStringList sl)
{
  foreach (QString s, sl)
    message(s);
}

QDjViewApplication::QDjViewApplication(int &argc, char **argv)
  : QApplication(argc, argv), 
    context(argv[0])
{
  // Message handler
  qInstallMsgHandler(qtMessageHandler);
  
  // Locale should not mess with printf
  // We do this again because old libdjvulibre
  // did not correctly set LC_NUMERIC.
#ifdef LC_ALL
  ::setlocale(LC_ALL, "");
# ifdef LC_NUMERIC
  ::setlocale(LC_NUMERIC, "C");
# endif
#endif
  
  // Mac/Cocoa bug workaround
#if defined(Q_WS_MAC) && defined(QT_MAC_USE_COCOA) && QT_VERSION<0x40503
  extern void qt_mac_set_native_menubar(bool);
  qt_mac_set_native_menubar(false);
#endif
  
  // Install translators
  QStringList langs = getTranslationLangs();
  QTranslator *qtTrans = new QTranslator(this);
  if (loadTranslator(qtTrans, "qt", langs))
    installTranslator(qtTrans);
  QTranslator *djviewTrans = new QTranslator(this);
  if (loadTranslator(djviewTrans, "djview", langs))
    installTranslator(djviewTrans);
}


QDjView * 
QDjViewApplication::newWindow()
{
  if (lastWindow && !lastWindow->getDocument())
    return lastWindow;
  QDjView *main = new QDjView(context);
  main->setAttribute(Qt::WA_DeleteOnClose);
  lastWindow = main;
  return main;
}


static void
addDirectory(QStringList &dirs, QString path)
{
  QString dirname = QDir::cleanPath(path);
  if (! dirs.contains(dirname))
    dirs << dirname;
}


static void
addLang(QStringList &langs, QString s)
{
  if (s.size() <= 0)
    return;
  if (! langs.contains(s))
    langs << s;
  QString sl = s.toLower();
  if (sl != s && ! langs.contains(sl))
    langs << sl;
}


QStringList 
QDjViewApplication::getTranslationDirs()
{
  if (translationDirs.isEmpty())
    {
      QStringList dirs;
      QDir dir = applicationDirPath();
      QString dirPath = dir.canonicalPath();
      addDirectory(dirs, dirPath);
#ifdef DIR_DATADIR
      QString datadir = DIR_DATADIR;
      addDirectory(dirs, datadir + "/djvu/djview4");
      addDirectory(dirs, datadir + "/djview4");
#endif
#ifdef Q_WS_MAC
      addDirectory(dirs, dirPath + "/Resources/$LANG.lproj");
      addDirectory(dirs, dirPath + "/../Resources/$LANG.lproj");
      addDirectory(dirs, dirPath + "/../../Resources/$LANG.lproj");
#endif
      addDirectory(dirs, dirPath + "/share/djvu/djview4");
      addDirectory(dirs, dirPath + "/share/djview4");
      addDirectory(dirs, dirPath + "/../share/djvu/djview4");
      addDirectory(dirs, dirPath + "/../share/djview4");
      addDirectory(dirs, dirPath + "/../../share/djvu/djview4");
      addDirectory(dirs, dirPath + "/../../share/djview4");
      addDirectory(dirs, "/usr/share/djvu/djview4");
      addDirectory(dirs, "/usr/share/djview4");
      addDirectory(dirs, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
      translationDirs = dirs;
    }
  return translationDirs;
}


QStringList 
QDjViewApplication::getTranslationLangs()
{
  if (translationLangs.isEmpty())
    {
      QStringList langs; 
      QString langOverride = QSettings().value("language").toString();
      if (! langOverride.isEmpty())
        addLang(langs, langOverride);
      QString varLanguage = ::getenv("LANGUAGE");
      if (varLanguage.size())
        foreach(QString lang, varLanguage.split(":", QString::SkipEmptyParts))
          addLang(langs, lang);
#ifdef LC_MESSAGES
      addLang(langs, QString(::setlocale(LC_MESSAGES, 0)));
#endif
#ifdef LC_ALL
      addLang(langs, QString(::setlocale(LC_ALL, 0)));
#endif
#ifdef Q_WS_MAC
      QSettings g(".", "globalPreferences");
      foreach (QString lang, g.value("AppleLanguages").toStringList())
        addLang(langs, lang);
#endif
      addLang(langs, QLocale::system().name());
      translationLangs = langs;
    }
  return translationLangs;
}


bool
QDjViewApplication::loadTranslator(QTranslator *trans, 
                                   QString name, QStringList langs)
{
  foreach (QString lang, langs)
    {
      foreach (QString dir, getTranslationDirs())
        {
          dir = dir.replace(QRegExp("\\$LANG(?!\\w)"), lang);
          QDir qdir(dir);
          if (qdir.exists())
            if (trans->load(name + "_" + lang, dir, "_.-"))
              return true;
          if (lang.startsWith("en_") || lang == "en" || lang == "c")
            break;
        }
    }
  return false;
}


bool 
QDjViewApplication::event(QEvent *ev)
{
  if (ev->type() == QEvent::FileOpen)
    {
      QString name = static_cast<QFileOpenEvent*>(ev)->file();
      QDjView *main = newWindow();
      if (main->open(name))
        {
          main->show();
        }
      else
        {
          message(tr("cannot open '%1'.").arg(name));
          delete main;
        }
      return true;
    }
  else if (ev->type() == QEvent::Close)
    {
      closeAllWindows();
    }
  return QApplication::event(ev);
}


#ifdef Q_WS_X11

void 
QDjViewApplication::commitData(QSessionManager &)
{
}

void 
QDjViewApplication::saveState(QSessionManager &sm)
{
  int n = 0;
  QSettings s;
  foreach(QWidget *w, topLevelWidgets())
    {
      QDjView *djview = qobject_cast<QDjView*>(w);
      if (djview && !djview->objectName().isEmpty() &&
          djview->getViewerMode() == QDjView::STANDALONE )
        {
          if (++n == 1)
            {
              QStringList discard = QStringList(applicationFilePath());
              discard << QLatin1String("-discard") << sessionId();
              sm.setDiscardCommand(discard);
              QStringList restart = QStringList(applicationFilePath());
              restart << QLatin1String("-session");
              restart << sessionId() + "_" + sessionKey();
              sm.setRestartCommand(restart);
              QString id = QLatin1String("session_") + sessionId();
              s.sync();
              s.remove(id);
              s.beginGroup(id);
            }
          s.beginGroup(QString::number(n));
          djview->saveSession(&s);
          s.endGroup();
        }
    }
  if (n > 0)
    {
      s.setValue("windows", n);
      s.endGroup();
      s.sync();
    }
}

#endif


#ifndef NPDJVU

static void 
usage()
{
  QString msg = QDjViewApplication::tr
    ("Usage: djview [options] [filename-or-url]\n"
     "Common options include:\n"
     "-help~~~Prints this message.\n"
     "-verbose~~~Prints all warning messages.\n"
     "-display <xdpy>~~~Select the X11 display <xdpy>.\n"
     "-geometry <xgeom>~~~Select the initial window geometry.\n"
     "-font <xlfd>~~~Select the X11 name of the main font.\n"
     "-style <qtstyle>~~~Select the QT user interface style.\n"
     "-fullscreen, -fs~~~Start djview in full screen mode.\n"
     "-page=<page>~~~Jump to page <page>.\n"
     "-zoom=<zoom>~~~Set zoom factor.\n"
     "-continuous=<yn>~~~Set continuous layout.\n"
     "-sidebyside=<yn>~~~Set side-by-side layout.\n" );
  
  // align tabs
  QStringList opts = msg.split("\n");
  int tab = 0;
  foreach (QString s, opts)
    tab = qMax(tab, s.indexOf("~~~"));
  foreach (QString s, opts)
    {
      int pos = s.indexOf("~~~");
      if (pos >= 0)
        s = QString(" %1  %2").arg(s.left(pos), -tab).arg(s.mid(pos+3));
      message(s, false);
    }
  exit(10);
}

int 
main(int argc, char *argv[])
{
  // Application data
  QApplication::setOrganizationName(DJVIEW_ORG);
  QApplication::setOrganizationDomain(DJVIEW_DOMAIN);
  QApplication::setApplicationName(DJVIEW_APP);
  
  // Message handler
  qInstallMsgHandler(qtMessageHandler);
#ifdef Q_OS_UNIX
  const char *s = ::getenv("DJVIEW_VERBOSE");
  if (s && strcmp(s,"0"))
    verbose = true;
#endif
  
  // Color specification 
  // (cause XRender errors under many versions of Qt/X11)
#ifndef Q_WS_X11
  QApplication::setColorSpec(QApplication::ManyColor);
#endif
  
  // Plugin mode
#ifdef Q_WS_X11
  if (argc==2 && !strcmp(argv[1],"-netscape"))
    {
      verbose = true;
      QDjViewPlugin dispatcher(argv[0]);
      return dispatcher.exec();
    }
#endif
  
  // Discard session
#ifdef Q_WS_X11
  if (argc==3 && !strcmp(argv[1],"-discard"))
    {
      QSettings s;
      s.remove(QLatin1String("session_")+QLatin1String(argv[2]));
      s.sync();
      return 0;
    }
#endif
  
  // Create application
  QDjViewApplication app(argc, argv);

  // Restore session
#ifdef Q_WS_X11
  if (app.isSessionRestored())
    {
      QSettings s;
      s.beginGroup(QLatin1String("session_") + app.sessionId());
      int windows = s.value("windows").toInt();
      if (windows > 0)
        {
          for (int n=1; n<=windows; n++)
            {
              QDjView *w = new QDjView(*app.djvuContext());
              w->setAttribute(Qt::WA_DeleteOnClose);
              s.beginGroup(QString::number(n));
              w->restoreSession(&s);
              s.endGroup();
              w->show();
            }
          return app.exec();
        }
    }
#endif
  
  // Process command line
  QDjView *main = app.newWindow();
  while (argc > 1 && argv[1][0] == '-')
    {
      QString arg = QString::fromLocal8Bit(argv[1]).replace(QRegExp("^-+"),"");
      if (arg == "help")
        usage();
      else if (arg == "verbose")
        verbose = true;
      else if (arg == "quiet")
        verbose = false;
      else if (arg == "fix")
        message(QApplication::tr("Option '-fix' is deprecated."));
      else 
        message(main->parseArgument(arg));
      argc --;
      argv ++;
    }
  if (argc > 2)
    usage();

  // Open file
  if (argc > 1)
    {
      QString name = QString::fromLocal8Bit(argv[1]);
      bool okay = true;
      if (name.contains(QRegExp("^[a-zA-Z]{3,8}:/")))
        okay = main->open(QUrl(name));
      else
        okay = main->open(name);
      if (! okay)
        {
          message(QDjView::tr("cannot open '%1'.").arg(argv[1]));
          exit(10);
        }
    }
  
  // Process events
  main->show();
  return app.exec();
}

#endif


/* -------------------------------------------------------------
   Local Variables:
   c++-font-lock-extra-types: ( "\\sw+_t" "[A-Z]\\sw*[a-z]\\sw*" )
   End:
   ------------------------------------------------------------- */
