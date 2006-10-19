//C-  -*- C++ -*-
//C- -------------------------------------------------------------------
//C- DjView4
//C- Copyright (c) 2006  Leon Bottou
//C-
//C- This software is subject to, and may be distributed under, the
//C- GNU General Public License. The license should have
//C- accompanied the software or you may obtain a copy of the license
//C- from the Free Software Foundation at http://www.fsf.org .
//C-
//C- This program is distributed in the hope that it will be useful,
//C- but WITHOUT ANY WARRANTY; without even the implied warranty of
//C- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//C- GNU General Public License for more details.
//C-  ------------------------------------------------------------------

// $Id$

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>

#include <QApplication>
#include <QByteArray>
#include <QDesktopWidget>
#include <QHBoxLayout>
#include <QList>
#include <QObject>
#include <QPair>
#include <QRegExp>
#include <QSet>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QX11EmbedWidget>

#include <QDebug>

#include "qdjvu.h"
#include "qdjview.h"
#include "qdjviewplugin.h"

#include <libdjvu/miniexp.h>
#include <libdjvu/ddjvuapi.h>



#ifdef Q_WS_X11
# include <X11/Xlib.h>
# include <QX11Info>




// ========================================
// STURCTURES
// ========================================


struct QDjViewPlugin::Saved
{
  int zoom;
  int rotation;
  bool sideBySide;
  bool continuous;
  QDjVuWidget::DisplayMode mode;
  QDjVuWidget::Position pos;

  void unpack(int q[4]);
  void pack(int q[4]);
  void save(QDjVuWidget *widget);
  void restore(QDjVuWidget *widget);
};


struct QDjViewPlugin::Stream : public QObject
{
  typedef QDjViewPlugin::Instance Instance;
  QUrl      url;
  Instance *instance;
  int       streamid;
  bool      started;
  bool      checked;
  bool      closed;
  ~Stream();
  Stream(int streamid, QUrl url, Instance *instance);
};


struct QDjViewPlugin::Instance
{
  QUrl                     url;
  QDjViewPlugin           *dispatcher;
  QDjViewPlugin::Document *document;
  QX11EmbedWidget         *embed;
  QDjView                 *djview;
  QStringList           args;
  QDjViewPlugin::Saved *saved;
  QDjView::ViewerMode   viewerMode;
  ~Instance();
  Instance(QDjViewPlugin *dispatcher);
  void open();
  void clean(QObject *object);
};
  





// ========================================
// SAVED
// ========================================


void 
QDjViewPlugin::Saved::unpack(int q[4])
{
  // embedding protocol only provides 4 integers
  zoom = q[0] & 0xfff;
  if (zoom & 0x800)
    zoom -= 0x1000;
  rotation = (q[0] & 0x3000) >> 12;
  sideBySide = !! (q[0] & 0x4000);
  continuous = !! (q[0] & 0x8000);
  mode = (QDjVuWidget::DisplayMode)((q[0] & 0xf0000) >> 16);
  pos.anchorRight = false;
  pos.anchorBottom = false;
  pos.inPage = false;
  pos.pageNo = q[1];
  pos.posView.rx() = q[2];
  pos.posView.ry() = q[3];
}


void 
QDjViewPlugin::Saved::pack(int q[4])
{
  q[0] = 0;
  q[1] = pos.pageNo;
  q[2] = pos.posView.x();
  q[3] = pos.posView.y();
  q[0] |= (rotation & 0x3) << 12;
  if (zoom >= 0)
    q[0] = zoom & 0x7ff;
  else
    q[0] = 0x1000 - ((- zoom) & 0x7ff);
  if (sideBySide)
    q[0] |= 0x4000;
  if (continuous)
    q[0] |= 0x8000;
  q[0] |= (((int)mode) & 0xf) << 16;
}


void 
QDjViewPlugin::Saved::save(QDjVuWidget *widget)
{
  zoom = widget->zoom();
  rotation = widget->rotation();
  sideBySide = widget->sideBySide();
  continuous = widget->continuous();
  mode = widget->displayMode();
  pos = widget->position();
}


void 
QDjViewPlugin::Saved::restore(QDjVuWidget *widget)
{
  widget->setZoom(zoom);
  widget->setRotation(rotation);
  widget->setSideBySide(sideBySide);
  widget->setContinuous(continuous);
  widget->setDisplayMode(mode);
  widget->setPosition(pos);
}



// ========================================
// STREAM
// ========================================


QDjViewPlugin::Stream::Stream(int streamid, QUrl url, Instance *instance)
  : url(url), instance(instance), streamid(streamid), 
    started(false), checked(false), closed(false)
{
  if (instance->dispatcher)
    instance->dispatcher->streamCreated(this);
}


QDjViewPlugin::Stream::~Stream()
{
  if (instance->dispatcher)
    instance->dispatcher->streamDestroyed(this);
}



// ========================================
// DOCUMENT
// ========================================



QDjViewPlugin::Document::Document(QDjViewPlugin::Instance *instance)
  : QDjVuDocument(true), instance(instance)
{
  QUrl docurl = QDjView::removeDjVuCgiArguments(instance->url);
  setUrl(instance->dispatcher->context, docurl);
  new QDjViewPlugin::Stream(0, docurl, instance);
}


void
QDjViewPlugin::Document::newstream(int streamid, QString, QUrl url)
{
  if (streamid > 0)
    {
      new QDjViewPlugin::Stream(streamid, url, instance);
      instance->dispatcher->getUrl(instance, url, QString());
    }
}




// ========================================
// FORWARDER
// ========================================



QDjViewPlugin::Forwarder::Forwarder(QDjViewPlugin *dispatcher)
  : QObject(), dispatcher(dispatcher)
{
}

void 
QDjViewPlugin::Forwarder::showStatus(QString message)
{
  QDjView *viewer = qobject_cast<QDjView*>(sender());
  Instance *instance = dispatcher->findInstance(viewer);
  if (instance)
    dispatcher->showStatus(instance, message);
}

void 
QDjViewPlugin::Forwarder::getUrl(QUrl url, QString target)
{
  QDjView *viewer = qobject_cast<QDjView*>(sender());
  Instance *instance = dispatcher->findInstance(viewer);
  if (! target.size())
    target = "_self";
  if (instance)
    dispatcher->getUrl(instance, url, target);
}

void 
QDjViewPlugin::Forwarder::quit()
{
  dispatcher->quit();
}

void 
QDjViewPlugin::Forwarder::dispatch()
{
  dispatcher->dispatch();
}

void 
QDjViewPlugin::Forwarder::lastViewerClosed()
{
  dispatcher->lastViewerClosed();
}

void 
QDjViewPlugin::Forwarder::clean(QObject *object)
{
  dispatcher->clean(object);
}




// ========================================
// INSTANCE
// ========================================


QDjViewPlugin::Instance::~Instance()
{
  delete saved;
  saved = 0;
  delete embed;
  embed = 0;
  delete djview;
  djview = 0;
  delete document;
  document = 0;
}


QDjViewPlugin::Instance::Instance(QDjViewPlugin *parent)
  : url(), dispatcher(parent), 
    document(0), embed(0), djview(0), saved(0)
{
}


void
QDjViewPlugin::Instance::open()
{
  if (!document && url.isValid() && embed && djview)
    {
      document = new QDjViewPlugin::Document(this);
      connect(document, SIGNAL(destroyed(QObject*)),
              dispatcher->forwarder, SLOT(clean(QObject*)) );
      djview->open(document, url);
      if (saved)
        saved->restore(djview->getDjVuWidget());        
      embed->show();
    }
}


void 
QDjViewPlugin::Instance::clean(QObject *object)
{
  if (object == embed)
    embed = 0;
  if (object == djview)
    djview = 0;
  if (object == document)
    document = 0;
}








// ========================================
// PROTOCOL
// ========================================


enum {
  TYPE_INTEGER = 1,
  TYPE_DOUBLE = 2,
  TYPE_STRING = 3,
  TYPE_POINTER = 4,
  TYPE_ARRAY = 5
};

enum {
  CMD_SHUTDOWN = 0,
  CMD_NEW = 1,
  CMD_DETACH_WINDOW = 2,
  CMD_ATTACH_WINDOW = 3,
  CMD_RESIZE = 4,
  CMD_DESTROY = 5,
  CMD_PRINT = 6,
  CMD_NEW_STREAM = 7,
  CMD_WRITE = 8,
  CMD_DESTROY_STREAM = 9,
  CMD_SHOW_STATUS = 10,
  CMD_GET_URL = 11,
  CMD_GET_URL_NOTIFY = 12,
  CMD_URL_NOTIFY = 13,
  CMD_HANDSHAKE = 14
};

#define OK_STRING   "OK"
#define ERR_STRING  "ERR"


void
QDjViewPlugin::write(int fd, const char *ptr, int size)
{
  while(size>0)
    {
      errno = 0;
      int res = ::write(fd, ptr, size);
      if (res<0 && errno==EINTR) 
        continue;
      if (res<=0) 
        throw res;
      size -= res; 
      ptr += res;
    }
}

void
QDjViewPlugin::read(int fd, char *buffer, int size)
{
  char *ptr = buffer;
  while(size>0)
    {
      errno = 0;
      int res = ::read(fd, ptr, size);
      if (res<0 && errno==EINTR)
        continue;
      if (res <= 0) 
        throw res;
      size -= res; 
      ptr += res;
    }
}


void
QDjViewPlugin::writeString(int fd, QByteArray s)
{
  int type = TYPE_STRING;
  int length = s.size();
  write(fd, (const char*)&type, sizeof(type));
  write(fd, (const char*)&length, sizeof(length));
  write(fd, s.data(), length+1);
}


void
QDjViewPlugin::writeString(int fd, QString x)
{
  writeString(fd, x.toUtf8());
}


void
QDjViewPlugin::writeInteger(int fd, int x)
{
  int type = TYPE_INTEGER;
  write(fd, (const char*)&type, sizeof(type));
  write(fd, (const char*)&x, sizeof(x));
}


void
QDjViewPlugin::writeDouble(int fd, double x)
{
  int type = TYPE_DOUBLE;
  write(fd, (const char*)&type, sizeof(type));
  write(fd, (const char*)&x, sizeof(x));
}


void
QDjViewPlugin::writePointer(int fd, const void *x)
{
  int type = TYPE_POINTER;
  write(fd, (const char*)&type, sizeof(type));
  write(fd, (const char*)&x, sizeof(x));
}


void
QDjViewPlugin::writeArray(int fd, QByteArray s)
{
  int type = TYPE_ARRAY;
  int length = s.size();
  write(fd, (const char*)&type, sizeof(type));
  write(fd, (const char*)&length, sizeof(length));
  write(fd, s.data(), length);
}


QString
QDjViewPlugin::readString(int fd)
{
  int type, length;
  read(fd, (char*)&type, sizeof(type));
  if (type != TYPE_STRING)
    throw 1;
  read(fd, (char*)&length, sizeof(length));
  if (length < 0) 
    throw 2;
  QByteArray utf(length+1, 0);
  read(fd, utf.data(), length+1);
  return QString::fromUtf8(utf);
}


QByteArray
QDjViewPlugin::readRawString(int fd)
{
  int type, length;
  read(fd, (char*)&type, sizeof(type));
  if (type != TYPE_STRING)
    throw 1;
  read(fd, (char*)&length, sizeof(length));
  if (length < 0) 
    throw 2;
  QByteArray x(length+1, 0);
  read(fd, x.data(), length+1);
  x.truncate(length);
  return x;
}


int
QDjViewPlugin::readInteger(int fd)
{
  int type;
  read(fd, (char*)&type, sizeof(type));
  if (type != TYPE_INTEGER)
    throw 1;
  int x;
  read(fd, (char*)&x, sizeof(x));
  return x;
}


double
QDjViewPlugin::readDouble(int fd)
{
  int type;
  read(fd, (char*)&type, sizeof(type));
  if (type != TYPE_DOUBLE)
    throw 1;
  double x;
  read(fd, (char*)&x, sizeof(x));
  return x;
}


void*
QDjViewPlugin::readPointer(int fd)
{
  int type;
  read(fd, (char*)&type, sizeof(type));
  if (type != TYPE_POINTER)
    throw 1;
  void *x;
  read(fd, (char*)&x, sizeof(x));
  return x;
}


QByteArray
QDjViewPlugin::readArray(int fd)
{
  int type, length;
  read(fd, (char*)&type, sizeof(type));
  if (type != TYPE_ARRAY)
    throw 1;
  read(fd, (char*)&length, sizeof(length));
  if (length < 0) 
    throw 2;
  QByteArray x(length, 0);
  read(fd, x.data(), length);
  return x;
}



// ========================================
// COMMANDS
// ========================================



void
QDjViewPlugin::cmdNew()
{
  // read arguments
  bool fullPage = !!readInteger(pipe_cmd);
  QString djvuDir = readString(pipe_cmd);
  int argc = readInteger(pipe_cmd);
  QStringList args;
  for (int i=0; i<argc; i++)
    {
      QString key = readString(pipe_cmd);
      QString val = readString(pipe_cmd);
      if (key.toLower() == "flags")
        args += val.split(QRegExp("\\s+"), QString::SkipEmptyParts);
      else if (key.toLower() != "src")
        args += key + QString("=") + val;
    }
  
  // read saved data
  Saved *saved = 0;
  if (readInteger(pipe_cmd))
    {
      Saved *saved = new Saved;
      int q[4];
      q[0] = readInteger(pipe_cmd);
      q[1] = readInteger(pipe_cmd);
      q[2] = readInteger(pipe_cmd);
      q[3] = readInteger(pipe_cmd);
      saved->unpack(q);
    }
  
  // create instance
  Instance *instance = new Instance(this);
  if (fullPage)
    instance->viewerMode = QDjView::FULLPAGE_PLUGIN;
  else
    instance->viewerMode = QDjView::EMBEDDED_PLUGIN;
  instance->saved = saved;
  instance->args = args;
  // success
  instances.insert(instance);
  writeString(pipe_reply, QByteArray(OK_STRING));
  writePointer(pipe_reply, (void*)instance);
}


void
QDjViewPlugin::cmdAttachWindow()
{
  Instance *instance = (Instance*) readPointer(pipe_cmd);
  QByteArray display = readRawString(pipe_cmd);
  QByteArray bgcolor = readRawString(pipe_cmd);
  Window window = (XID)readInteger(pipe_cmd);
  (XID)readInteger(pipe_cmd);  // colormap
  (XID)readInteger(pipe_cmd);  // visualid
  int width = readInteger(pipe_cmd);
  int height = readInteger(pipe_cmd);
  
  if (!instances.contains(instance))
    {
      fprintf(stderr, "djview dispatcher: bad instance\n");
      writeString(pipe_reply, QByteArray(ERR_STRING));
      return;
    }
  // create application object
  if (! application)
    {
      Display *dpy = XOpenDisplay(display);
      if (!dpy)
        {
          fprintf(stderr,"djview dispatcher: "
                  "cannot open display %s\n", (const char*)display);
          throw 2;
        }
      int argc = 0;
      const char *argv[8];
      argv[argc++] = progname;
      argv[argc++] = "-display";
      argv[argc++] = (const char*) display;
#if COPY_BACKGROUND_COLOR
      if (bgcolor.size() > 0)
        {
          argv[argc++] = "-bg";
          argv[argc++] = (const char*) bgcolor;
        }
#endif
      argv[argc++] = 0;
      application = new QApplication(argc, const_cast<char**>(argv));
      application->setQuitOnLastWindowClosed(false);
      context = new QDjVuContext(progname);
      forwarder = new Forwarder(this);
      connect(application, SIGNAL(lastWindowClosed()), 
              forwarder, SLOT(lastViewerClosed()));
      notifier = new QSocketNotifier(pipe_cmd, QSocketNotifier::Read); 
      connect(notifier, SIGNAL(activated(int)), 
              forwarder, SLOT(dispatch()));
      timer = new QTimer();
      timer->setSingleShot(true);
      timer->start(5*60*1000);
      connect(timer, SIGNAL(timeout()), 
              forwarder, SLOT(quit()));
      XCloseDisplay(dpy);
    }
  
  // create djview object
  QX11EmbedWidget *embed = instance->embed;
  QDjView *djview = instance->djview;
  if (! embed)
    {
      embed = new QX11EmbedWidget();
      instance->embed = embed;
      connect(embed, SIGNAL(destroyed(QObject*)),
              forwarder, SLOT(clean(QObject*)) );
      QLayout *layout = new QHBoxLayout(embed);
      layout->setMargin(0);
      layout->setSpacing(0);
      djview = new QDjView(*context, instance->viewerMode, embed);
      Qt::WindowFlags flags = djview->windowFlags() & ~Qt::WindowType_Mask;
      djview->setWindowFlags(flags | Qt::Widget);
      instance->djview = djview;
      layout->addWidget(djview);
      connect(djview, SIGNAL(destroyed(QObject*)),
              forwarder, SLOT(clean(QObject*)) );
      connect(djview, SIGNAL(pluginStatusMessage(QString)),
              forwarder, SLOT(showStatus(QString)) );
      connect(djview, SIGNAL(pluginGetUrl(QUrl,QString)),
              forwarder, SLOT(getUrl(QUrl,QString)) );
      // apply arguments
      foreach (QString s, instance->args)
        djview->parseArgument(s);
    }
  // map and reparent djview object
  embed->setGeometry(0, 0, width, height);
  embed->embedInto(window);
  qDebug() << "attach" << embed << embed->winId() << "to" << window;
  instance->open();
  writeString(pipe_reply, QByteArray(OK_STRING));
  timer->stop();
}


void
QDjViewPlugin::cmdDetachWindow()
{
  Instance *instance = (Instance*) readPointer(pipe_cmd);
  if (instances.contains(instance))
    {
      if (instance->embed && application)
        {
          if (!instance->saved)
            instance->saved = new Saved;
          if (instance->saved)
            instance->saved->save(instance->djview->getDjVuWidget());
          instance->djview->close();
          instance->embed->hide();
          instance->embed->deleteLater();
          instance->embed = 0;
          instance->djview = 0;
        }
    }
  writeString(pipe_reply, QByteArray(OK_STRING));
}


void
QDjViewPlugin::cmdResize()
{
  Instance *instance = (Instance*) readPointer(pipe_cmd);
  int width = readInteger(pipe_cmd);
  int height = readInteger(pipe_cmd);
  if (instances.contains(instance))
    if (instance->embed && application && width>0 && height>0)
      instance->embed->resize(width, height);
  writeString(pipe_reply, QByteArray(OK_STRING));
}


void
QDjViewPlugin::cmdDestroy()
{
  Saved saved;
  Instance *instance = (Instance*) readPointer(pipe_cmd);
  int q[4];
  q[0] = q[1] = q[2] = q[3] = 0;
  if (instances.contains(instance))
    {
      if (instance->djview)
        instance->djview->close();
      instance->djview = 0;
      if (instance->embed)
        instance->embed->deleteLater();
      instance->embed = 0;
      if (instance->saved)
        instance->saved->pack(q);
      QList<Stream*> streamList = streams.toList();
      foreach(Stream *stream, streamList)
        if (stream->instance == instance)
          delete(stream);
      delete instance;
      instances.remove(instance);
    }  
  writeString(pipe_reply, QByteArray(OK_STRING));
  writeInteger(pipe_reply, q[0]);
  writeInteger(pipe_reply, q[1]);
  writeInteger(pipe_reply, q[2]);
  writeInteger(pipe_reply, q[3]);
}


void
QDjViewPlugin::cmdNewStream()
{
  // read
  Instance *instance = (Instance*) readPointer(pipe_cmd);
  QUrl url = QUrl::fromEncoded(readRawString(pipe_cmd));
  // check
  if (! url.isValid() )
    {
      fprintf(stderr,"djview dispatcher: invalid url '%s'\n",
              (const char*) url.toEncoded());
      writeString(pipe_reply, QByteArray(ERR_STRING));
      return;
    }
  // proceed
  Stream *stream = 0;
  if (instances.contains(instance))
    {
      // first stream
      if (! streams.size())
        {
          instance->url = url;
          instance->open();
          url = QDjView::removeDjVuCgiArguments(url);
        }
      // search stream
      foreach(Stream *s, streams)
        if (!stream && !s->started)
          if (s->instance == instance && s->url == url)
            stream = s;
    }
  // mark
  if (stream)
    stream->started = true;
  // reply (null means discard stream)
  writeString(pipe_reply, QByteArray(OK_STRING));
  writePointer(pipe_reply, (void*)stream);
}


void
QDjViewPlugin::cmdWrite()
{
  Stream *stream = (Stream*) readPointer(pipe_cmd);
  QByteArray data = readArray(pipe_cmd);
  // locate stream
  if (!streams.contains(stream) || stream->closed)
    {
      // -- null return value means that stream must be discarded.
      writeString(pipe_reply, QByteArray(OK_STRING));
      writeInteger(pipe_reply, 0);
      return;
    }
  // check stream
  if (!stream->checked && data.size()>0)
    {
      stream->checked = true;
      // -- if the stream is not a djvu file, 
      //    this is probably a html error message.
      //    But we need at least 8 bytes to check
      if (data.size() >= 8)
        {
          const char *cdata = (const char*)data;
          if (strncmp(cdata, "FORM", 4) && strncmp(cdata+4, "FORM", 4))
            {
              getUrl(stream->instance, stream->url, QString("_self")); 
              delete stream;
              writeString(pipe_reply, QByteArray(OK_STRING));
              writeInteger(pipe_reply, 0);
              return;
            }
        }
    }
  // pass data
  Document *document = stream->instance->document;
  ddjvu_stream_write(*document, stream->streamid, data, data.size());
  writeString(pipe_reply, QByteArray(OK_STRING));
  writeInteger(pipe_reply, data.size());
}


void
QDjViewPlugin::cmdDestroyStream()
{
  Stream *stream = (Stream*) readPointer(pipe_cmd);
  int okay = readInteger(pipe_cmd);
  if (streams.contains(stream))
    {
      Document *document = stream->instance->document;
      if (document && !stream->closed)
        ddjvu_stream_close(*document, stream->streamid, !okay);
      stream->closed = true;
      delete stream;
    }
  writeString(pipe_reply, QByteArray(OK_STRING));
}


void
QDjViewPlugin::cmdUrlNotify()
{
  QUrl url = QUrl::fromEncoded(readRawString(pipe_cmd));
  readInteger(pipe_cmd); // notification code (unused)
  Stream *stream = 0;
  QList<Stream*> streamList = streams.toList();
  foreach(stream, streamList)
    if (!stream->started && stream->url == url)
      delete stream;
  writeString(pipe_reply, QByteArray(OK_STRING));
}


void
QDjViewPlugin::cmdPrint()
{
  Instance *instance = (Instance*) readPointer(pipe_cmd);
  readInteger(pipe_cmd); // full mode (unused)
  if (instances.contains(instance))
    if (instance->djview)
      QTimer::singleShot(0, instance->djview, SLOT(print()));
  writeString(pipe_reply, QByteArray(OK_STRING));
}


void
QDjViewPlugin::cmdHandshake()
{
  writeString(pipe_reply, QByteArray(OK_STRING));
}


void
QDjViewPlugin::cmdShutdown()
{
  QList<Stream*> streamList = streams.toList();
  QList<Instance*> instanceList = instances.toList();
  foreach(Stream *s, streamList) 
    delete(s);
  foreach(Instance *s, instanceList)
    delete(s);
  streams.clear();
  instances.clear();
  lastViewerClosed();  
}





// ========================================
// QDJVIEWPLUGIN
// ========================================



static QDjViewPlugin *thePlugin;


QDjViewPlugin::~QDjViewPlugin()
{
  thePlugin = 0;
  QList<Stream*> streamList = streams.toList();
  QList<Instance*> instanceList = instances.toList();
  foreach(Stream *s, streamList) 
    delete(s);
  foreach(Instance *s, instanceList)
    delete(s);
  delete forwarder;
  forwarder = 0;
  delete notifier;
  notifier = 0;
  delete timer;
  timer = 0;
  delete context;
  context = 0;
  delete application;
  application = 0;
}


QDjViewPlugin::QDjViewPlugin(const char *progname)
  : progname(progname),
    context(0),
    timer(0),
    notifier(0),
    application(0),
    forwarder(0),
    pipe_cmd(3),
    pipe_reply(4),
    pipe_request(5),
    return_code(0),
    quit_flag(false)
{
  // Disable SIGPIPE
#ifdef SIGPIPE
# ifdef SA_RESTART
  sigset_t mask;
  struct sigaction act;
  sigemptyset(&mask);
  sigaddset(&mask, SIGPIPE);
  sigprocmask(SIG_BLOCK, &mask, 0);
  sigaction(SIGPIPE, 0, &act);
  act.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &act, 0);
# else
  signal(SIGPIPE, SIG_IGN);
# endif
#endif
  // Set global dispatcher
  if (thePlugin)
    qWarning("Constructing multiple dispatchers");
  thePlugin = this;
}


QDjViewPlugin *
QDjViewPlugin::instance()
{
  return thePlugin;
}


int 
QDjViewPlugin::exec()
{
#if 1
  int loop = getenv("DEBUGDJVIEW") ? 1 : 0;
  while (loop)
    sleep(1);
#endif
  try 
    {
      qDebug() << "--- START EXEC";
      // startup message
      writeString(pipe_reply, QByteArray("Here am I"));
      // dispatch until we get display
      while (!application && !quit_flag)
        dispatch();
      // handle events
      if (!quit_flag)
        application->exec();
      qDebug() << "--- END EXEC";
    }
  catch(int err)
    {
      reportError(err);
      return_code = 10;
    }
  return return_code;
}


void 
QDjViewPlugin::exit(int retcode)
{
  return_code = retcode;
  quit_flag = true;
  if (timer)
    timer->stop();
  if (application)
    application->exit(retcode);
}


void 
QDjViewPlugin::quit()
{
  this->exit(0);
}


void 
QDjViewPlugin::reportError(int err)
{
  if (err < 0)
    perror("djview dispatcher");
  else if (err == 0)
    fprintf(stderr, "djview dispatcher: unexpected end of file\n");
  else
    fprintf(stderr, "djview dispatcher: protocol error (%d)\n", err);
}


void 
QDjViewPlugin::streamCreated(Stream *stream)
{
  streams.insert(stream);
}


void 
QDjViewPlugin::streamDestroyed(Stream *stream)
{
  if (streams.contains(stream))
    {
      if (instances.contains(stream->instance))
        {
          Document *document = stream->instance->document;
          if (document && !stream->closed)
            ddjvu_stream_close(*document, stream->streamid, true);
        }
      stream->closed = true;
      stream->started = stream->checked = false;
      streams.remove(stream);
    }
}


QDjViewPlugin::Instance*
QDjViewPlugin::findInstance(QDjView *djview)
{
  foreach(Instance *instance, instances)
    if (djview && instance->djview == djview)
      return instance;
  return 0;
}


void
QDjViewPlugin::getUrl(Instance *instance, QUrl url, QString target)
{
  qDebug() << "getUrl" << instance << url << target;
  try
    {
      writeInteger(pipe_request, CMD_GET_URL);
      writePointer(pipe_request, (void*) instance);
      writeString(pipe_request, url.toEncoded());
      writeString(pipe_request, target);
    }
  catch(int err)
    {
      reportError(err);
      return_code = 10;
      quit_flag = true;
    }
}


void
QDjViewPlugin::showStatus(Instance *instance, QString message)
{
  try
    {
      message.replace(QRegExp("\\s"), " ");
      writeInteger(pipe_request, CMD_SHOW_STATUS);
      writePointer(pipe_request, (void*) instance);
      writeString(pipe_request, message);
    }
  catch(int err)
    {
      reportError(err);
      return_code = 10;
      quit_flag = true;
    }
}



void
QDjViewPlugin::dispatch()
{
  try
    {
      int cmd = readInteger(pipe_cmd);
      qDebug() << "dispatch" << cmd;
      switch (cmd)
        {
        case CMD_SHUTDOWN:       cmdShutdown(); break;
        case CMD_NEW:            cmdNew(); break;
        case CMD_DETACH_WINDOW:  cmdDetachWindow(); break;
        case CMD_ATTACH_WINDOW:  cmdAttachWindow(); break;
        case CMD_RESIZE:         cmdResize(); break;
        case CMD_DESTROY:        cmdDestroy(); break;
        case CMD_PRINT:          cmdPrint(); break;
        case CMD_NEW_STREAM:     cmdNewStream(); break;
        case CMD_WRITE:          cmdWrite(); break;
        case CMD_DESTROY_STREAM: cmdDestroyStream(); break;
        case CMD_URL_NOTIFY:     cmdUrlNotify(); break;
        case CMD_HANDSHAKE:      cmdHandshake(); break;
        default:                 throw 3;
        }
    }
  catch(int err)
    {
      if (err)
        reportError(err);
      if (err)
        exit(10);
      else
        exit(0);
    }
}


void
QDjViewPlugin::lastViewerClosed()
{
  if (timer)
    {
      timer->stop();
      timer->start(5*60*1000);
    }
  else
    {
      quit_flag = true;
    }
}


void
QDjViewPlugin::clean(QObject *object)
{
  foreach(Instance *instance, instances)
    instance->clean(object);
}




// ----------------------------------------
// END


#endif // Q_WS_X11

/* -------------------------------------------------------------
   Local Variables:
   c++-font-lock-extra-types: ( "\\sw+_t" "[A-Z]\\sw*[a-z]\\sw*" )
   End:
   ------------------------------------------------------------- */