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

#include "stdlib.h"
#include "math.h"

#include <QAbstractItemDelegate>
#include <QAbstractListModel>
#include <QAction>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QPixmap>
#include <QRegExp>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>

#include "qdjvu.h"
#include "qdjvuwidget.h"
#include "qdjviewsidebar.h"
#include "qdjview.h"




// =======================================
// QDJVIEWOUTLINE
// =======================================




QDjViewOutline::QDjViewOutline(QDjView *djview)
  : QWidget(djview), 
    djview(djview), 
    loaded(false)
{
  tree = new QTreeWidget(this);
  tree->setColumnCount(1);
  tree->setItemsExpandable(true);
  tree->setUniformRowHeights(true);
  tree->header()->hide();
  tree->header()->setStretchLastSection(true);
  tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree->setSelectionMode(QAbstractItemView::SingleSelection);
  tree->setTextElideMode(Qt::ElideRight);
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setMargin(0);
  layout->setSpacing(0);
  layout->addWidget(tree);

  connect(tree, SIGNAL(itemActivated(QTreeWidgetItem*, int)),
          this, SLOT(itemActivated(QTreeWidgetItem*)) );
  connect(djview, SIGNAL(documentClosed(QDjVuDocument*)),
          this, SLOT(clear()) );
  connect(djview, SIGNAL(documentOpened(QDjVuDocument*)),
          this, SLOT(clear()) );
  connect(djview, SIGNAL(documentReady(QDjVuDocument*)),
          this, SLOT(refresh()) );
  connect(djview->getDjVuWidget(), SIGNAL(pageChanged(int)),
          this, SLOT(pageChanged(int)) );
  connect(djview->getDjVuWidget(), SIGNAL(layoutChanged()),
          this, SLOT(refresh()) );
  
  setWhatsThis(tr("<html><b>Document outline.</b><br/> "
                  "This panel display the document outline, "
                  "or the page names when the outline is not available, "
                  "Double-click any entry to jump to the selected page."
                  "</html>"));
  
  if (djview->pageNum() > 0)
    refresh();
}


void 
QDjViewOutline::clear()
{
  tree->clear();
  loaded = false;
}

void 
QDjViewOutline::refresh()
{
  QDjVuDocument *doc = djview->getDocument();
  if (doc && !loaded && djview->pageNum()>0)
    {
      miniexp_t outline = doc->getDocumentOutline();
      if (outline == miniexp_dummy)
        return;
      loaded = true;
      if (outline)
        {
          if (!miniexp_consp(outline) ||
              miniexp_car(outline) != miniexp_symbol("bookmarks"))
            {
              QString msg = tr("Outline data is corrupted");
              qWarning((const char*)msg.toLocal8Bit());
            }
          tree->clear();
          QTreeWidgetItem *root = new QTreeWidgetItem();
          fillItems(root, miniexp_cdr(outline));
          QTreeWidgetItem *item = root;
          while (item->childCount()==1 &&
                 item->data(0,Qt::UserRole).toInt() >= 0)
            item = item->child(0);
          while (item->childCount() > 0)
            tree->insertTopLevelItem(tree->topLevelItemCount(),
                                     item->takeChild(0) );
          delete root;
        }
      else
        {
          tree->clear();
          QTreeWidgetItem *root = new QTreeWidgetItem(tree);
          root->setText(0, tr("Pages"));
          root->setFlags(0);
          root->setData(0, Qt::UserRole, -1);
          for (int pageno=0; pageno<djview->pageNum(); pageno++)
            {
              QTreeWidgetItem *item = new QTreeWidgetItem(root);
              QString name = djview->pageName(pageno);
              item->setText(0, tr("Page %1").arg(name));
              item->setData(0, Qt::UserRole, pageno);
              item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
              item->setToolTip(0, tr("Go to page %1").arg(name));
              item->setWhatsThis(0, whatsThis());
            }
          tree->setItemExpanded(root, true);
        }
      pageChanged(djview->getDjVuWidget()->page());
    }
}


void 
QDjViewOutline::fillItems(QTreeWidgetItem *root, miniexp_t expr)
{
  while(miniexp_consp(expr))
    {
      miniexp_t s = miniexp_car(expr);
      expr = miniexp_cdr(expr);
      if (miniexp_consp(s) &&
          miniexp_consp(miniexp_cdr(s)) &&
          miniexp_stringp(miniexp_car(s)) &&
          miniexp_stringp(miniexp_cadr(s)) )
        {
          // fill item
          const char *name = miniexp_to_str(miniexp_car(s));
          const char *page = miniexp_to_str(miniexp_cadr(s));
          QTreeWidgetItem *item = new QTreeWidgetItem(root);
          item->setText(0, QString::fromUtf8(name));
          int pageno = -1;
          if (page[0]=='#' && page[1]!='+' && page[1]!='-')
            pageno = djview->pageNumber(QString::fromUtf8(page));
          item->setData(0, Qt::UserRole, pageno);
          item->setFlags(0);
          item->setWhatsThis(0, whatsThis());
          if (pageno >= 0)
            {
              QString name = djview->pageName(pageno);
              item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
              item->setToolTip(0, tr("Go to page %1").arg(name));
            }
          // recurse
          fillItems(item, miniexp_cddr(s));
        }
    }
}


void 
QDjViewOutline::pageChanged(int pageno)
{
  int fp = -1;
  QTreeWidgetItem *fi = 0;
  // find current selection
  QList<QTreeWidgetItem*> sel = tree->selectedItems();
  QTreeWidgetItem *si = 0;
  if (sel.size() == 1)
    si = sel[0];
  // current selection has priority
  if (si)
    searchItem(si, pageno, fi, fp);
  // search
  for (int i=0; i<tree->topLevelItemCount(); i++)
    searchItem(tree->topLevelItem(i), pageno, fi, fp);
  // select
  if (si && fi && si != fi)
    tree->setItemSelected(si, false);
  if (fi && si != fi)
    {
      tree->setCurrentItem(fi);
      tree->setItemSelected(fi, true);
      tree->scrollToItem(fi);
    }
}


void 
QDjViewOutline::searchItem(QTreeWidgetItem *item, int pageno, 
                           QTreeWidgetItem *&fi, int &fp)
{
  int page = item->data(0, Qt::UserRole).toInt();
  if (page>=0 && page<=pageno && page>fp)
    {
      fi = item;
      fp = page;
    }
  for (int i=0; i<item->childCount(); i++)
    searchItem(item->child(i), pageno, fi, fp);
}


void 
QDjViewOutline::itemActivated(QTreeWidgetItem *item)
{
  int pageno = item->data(0, Qt::UserRole).toInt();
  if (pageno >= 0 && pageno < djview->pageNum())
    djview->goToPage(pageno);
}







// =======================================
// QDJVIEWTHUMBNAILS
// =======================================




class QDjViewThumbnails::View : public QListView
{
  Q_OBJECT
public:
  View(QDjViewThumbnails *widget);
protected:
  QStyleOptionViewItem viewOptions() const;
private:
  QDjViewThumbnails *widget;
  QDjView *djview;
};


class QDjViewThumbnails::Model : public QAbstractListModel
{
  Q_OBJECT
public:
  ~Model();
  Model(QDjViewThumbnails*);
  virtual int rowCount(const QModelIndex &parent) const;
  virtual QVariant data(const QModelIndex &index, int role) const;
  int getSize() { return size; }
  int getSmart() { return smart; }
public slots:
  void setSize(int);
  void setSmart(bool);
  void scheduleRefresh();
protected slots:
  void documentClosed(QDjVuDocument *doc);
  void documentReady(QDjVuDocument *doc);
  void thumbnail(int);
  void refresh();
private:
  QDjView *djview;
  QDjViewThumbnails *widget;
  QStringList names;
  ddjvu_format_t *format;
  QIcon icon;
  int size;
  bool smart;
  bool refreshScheduled;
  int  pageInProgress;
  QIcon makeIcon(int pageno) const;
  QSize makeHint(int pageno) const;
};



// ----------------------------------------
// QDJVIEWTHUMBNAILS::VIEW


QDjViewThumbnails::View::View(QDjViewThumbnails *widget)
  : QListView(widget), 
    widget(widget), 
    djview(widget->djview)
{
  setDragEnabled(false);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setTextElideMode(Qt::ElideRight);
  setViewMode(QListView::IconMode);
  setFlow(QListView::LeftToRight);
  setWrapping(true);
  setMovement(QListView::Static);
  setResizeMode(QListView::Adjust);
  setLayoutMode(QListView::Batched);
  setSpacing(8);
#if QT_VERSION >= 0x040100
  setUniformItemSizes(true);
#endif
#if QT_VERSION < 0x040100
  // Hack to request thumbnail computations because
  // we cannot do it efficiently in Model::makeData().
  connect((QObject*)verticalScrollBar(), SIGNAL(sliderMoved(int)),
          (QObject*)widget->model, SLOT(scheduleRefresh()) );
#endif
}


QStyleOptionViewItem 
QDjViewThumbnails::View::viewOptions() const
{
  int size = widget->model->getSize();
  QStyleOptionViewItem opt = QListView::viewOptions();
  opt.decorationAlignment = Qt::AlignCenter;
  opt.decorationPosition = QStyleOptionViewItem::Top;
  opt.decorationSize = QSize(size, size);
  opt.displayAlignment = Qt::AlignCenter;
  return opt;
}



// ----------------------------------------
// QDJVIEWTHUMBNAILS::MODEL


QDjViewThumbnails::Model::~Model()
{
  if (format)
    ddjvu_format_release(format);
}


QDjViewThumbnails::Model::Model(QDjViewThumbnails *widget)
  : QAbstractListModel(widget),
    djview(widget->djview), 
    widget(widget), 
    format(0),
    size(0),
    smart(true),
    refreshScheduled(false),
    pageInProgress(-1)
{
  // create format
  unsigned int masks[4] = { 0xff0000, 0xff00, 0xff, 0xff000000 };
  format = ddjvu_format_create(DDJVU_FORMAT_RGBMASK32, 3, masks);
  ddjvu_format_set_row_order(format, true);
  ddjvu_format_set_y_direction(format, true);
  ddjvu_format_set_ditherbits(format, QPixmap::defaultDepth());
  // set size
  setSize(64);
  // connect
  connect(djview, SIGNAL(documentClosed(QDjVuDocument*)),
          this, SLOT(documentClosed(QDjVuDocument*)) );
  connect(djview, SIGNAL(documentReady(QDjVuDocument*)),
          this, SLOT(documentReady(QDjVuDocument*)) );
  // update
  if (djview->pageNum() > 0)
    documentReady(djview->getDocument());
}


void 
QDjViewThumbnails::Model::documentClosed(QDjVuDocument *doc)
{
  names.clear();
  disconnect(doc, 0, this, 0);
  emit layoutChanged();
}


void 
QDjViewThumbnails::Model::documentReady(QDjVuDocument *doc)
{
  names.clear();
  int pagenum = djview->pageNum();
  for (int pageno=0; pageno<pagenum; pageno++)
    names << djview->pageName(pageno);
  connect(doc, SIGNAL(thumbnail(int)),
          this, SLOT(thumbnail(int)) );
  connect(doc, SIGNAL(pageinfo()),
          this, SLOT(scheduleRefresh()) );
  connect(doc, SIGNAL(idle()),
          this, SLOT(scheduleRefresh()) );
  emit layoutChanged();
  widget->pageChanged(djview->getDjVuWidget()->page());
}


void 
QDjViewThumbnails::Model::thumbnail(int pageno)
{
  QModelIndex mi = index(pageno);
  emit dataChanged(mi, mi);
  scheduleRefresh();
}


void
QDjViewThumbnails::Model::scheduleRefresh()
{
  if (! refreshScheduled)
    QTimer::singleShot(0, this, SLOT(refresh()));
  refreshScheduled = true;
}


void
QDjViewThumbnails::Model::refresh()
{
  QDjVuDocument *doc = djview->getDocument();
  ddjvu_status_t status;
  refreshScheduled = false;
  if (doc && pageInProgress >= 0)
    {
      status = ddjvu_thumbnail_status(*doc, pageInProgress, 0);
      if (status >= DDJVU_JOB_OK)
        pageInProgress = -1;
    }
  if (doc && pageInProgress < 0)
    {
      QRect dr = widget->view->rect();
      for (int i=0; i<names.size(); i++)
        {
          QModelIndex mi = index(i);
          if (dr.intersects(widget->view->visualRect(mi)))
            {
              status = ddjvu_thumbnail_status(*doc, i, 0);
              if (status == DDJVU_JOB_NOTSTARTED)
                {
                  if (smart && !ddjvu_document_check_pagedata(*doc, i))
                    continue;
                  status = ddjvu_thumbnail_status(*doc, i, 1);
                  if (status == DDJVU_JOB_STARTED)
                    {
                      pageInProgress = i;
                      break;
                    }
                }
            }
        }
    }
}


void 
QDjViewThumbnails::Model::setSmart(bool b)
{
  if (b != smart)
    {
      smart = b;
      scheduleRefresh();
    }
}


void 
QDjViewThumbnails::Model::setSize(int newSize)
{
  newSize = qBound(16, newSize, 256);
  if (newSize != size)
    {
      size = newSize;
      QPixmap pixmap(size,size);
      pixmap.fill();
      QPainter painter;
      int s8 = size/8;
      if (s8 >= 1)
        {
          QPolygon poly;
          poly << QPoint(s8,0)
               << QPoint(size-2*s8,0) 
               << QPoint(size-s8-1,s8) 
               << QPoint(size-s8-1,size-1) 
               << QPoint(s8,size-1);
          QPainter painter(&pixmap);
          painter.setBrush(Qt::NoBrush);
          painter.setPen(Qt::darkGray);
          painter.drawPolygon(poly);
        }
      icon = QIcon(pixmap);
    }
  emit layoutChanged();
}


QIcon
QDjViewThumbnails::Model::makeIcon(int pageno) const
{
  QDjVuDocument *doc = djview->getDocument();
  if (doc)
    {
      // render thumbnail
      int w = size;
      int h = size;
      QImage img(size, size, QImage::Format_RGB32);
      if (ddjvu_thumbnail_render(*doc, pageno, &w, &h, format, 
                                 img.bytesPerLine(), (char*)img.bits() ) )
        {
          QPixmap pixmap(size,size);
          pixmap.fill();
          QPoint dst((size-w)/2, (size-h)/2);
          QRect src(0,0,w,h);
          QPainter painter;
          painter.begin(&pixmap);
          painter.drawImage(dst, img, src);
          painter.setBrush(Qt::NoBrush);
          painter.setPen(Qt::darkGray);
          painter.drawRect(dst.x(), dst.y(), w-1, h-1);
          painter.end();
          return QIcon(pixmap);
        }
#if QT_VERSION >= 0x40100
      // We know this is not a sizehint request because the
      // sizehint code calls Model::data(Qt::SizeHintRole) first.
      // Therefore it must be a paint request and we can
      // safely request thumbnail calculations.
      else if (ddjvu_thumbnail_status(*doc,pageno,0)==DDJVU_JOB_NOTSTARTED)
        const_cast<Model*>(this)->scheduleRefresh();
#endif
    }
  return icon;
}


QSize 
QDjViewThumbnails::Model::makeHint(int) const
{
  QFontMetrics metrics(widget->view->font());
  return QSize(size, size+metrics.height());
}


int 
QDjViewThumbnails::Model::rowCount(const QModelIndex &) const
{
  return names.size();
}


QVariant 
QDjViewThumbnails::Model::data(const QModelIndex &index, int role) const
{
  if (index.isValid())
    {
      int pageno = index.row();
      if (pageno>=0 && pageno<names.size())
        {
          switch(role)
            {
            case Qt::DisplayRole: 
            case Qt::ToolTipRole:
              return names[pageno];
            case Qt::DecorationRole:
              return makeIcon(pageno);
            case Qt::WhatsThisRole:
              return widget->whatsThis();
            case Qt::UserRole:
              return pageno;
#if QT_VERSION >= 0x040100
            case Qt::SizeHintRole:
              return makeHint(pageno);
#endif
            default:
              break;
            }
        }
    }
  return QVariant();
}






// ----------------------------------------
// QDJVIEWTHUMBNAILS


QDjViewThumbnails::QDjViewThumbnails(QDjView *djview)
  : QWidget(djview),
    djview(djview)
{
  model = new Model(this);
  selection = new QItemSelectionModel(model);
  view = new View(this);
  view->setModel(model);
  view->setSelectionModel(selection);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setMargin(0);
  layout->setSpacing(0);
  layout->addWidget(view);
  
  connect(djview->getDjVuWidget(), SIGNAL(pageChanged(int)),
          this, SLOT(pageChanged(int)) );
  connect(view, SIGNAL(activated(const QModelIndex&)),
          this, SLOT(activated(const QModelIndex&)) );
  
  menu = new QMenu(this);
  QActionGroup *group = new QActionGroup(this);
  QAction *action;
  action = menu->addAction(tr("Tiny","thumbnail menu"));
  connect(action,SIGNAL(triggered()),this,SLOT(setSize()) );
  action->setCheckable(true);
  action->setActionGroup(group);
  action->setData(32);
  action = menu->addAction(tr("Small","thumbnail menu"));
  connect(action,SIGNAL(triggered()),this,SLOT(setSize()) );
  action->setCheckable(true);  
  action->setActionGroup(group);
  action->setData(64);
  action = menu->addAction(tr("Medium","thumbnail menu"));
  connect(action,SIGNAL(triggered()),this,SLOT(setSize()) );
  action->setCheckable(true);
  action->setActionGroup(group);
  action->setData(96);
  action = menu->addAction(tr("Large","thumbnail menu"));
  connect(action,SIGNAL(triggered()),this,SLOT(setSize()) );
  action->setCheckable(true);
  action->setActionGroup(group);
  action->setData(160);
  menu->addSeparator();
  action = menu->addAction(tr("Smart","thumbnail menu"));
  connect(action,SIGNAL(toggled(bool)),this,SLOT(setSmart(bool)) );
  action->setCheckable(true);
  action->setData(true);
  updateActions();

#if Q_WS_MAC
  QString mc = tr("Control Left Mouse Button");
#else
  QString mc = tr("Right Mouse Button");
#endif
  setWhatsThis(tr("<html><b>Document thumbnails.</b><br/> "
                  "This panel display thumbnails for the document pages. "
                  "Double click a thumbnail to jump to the selected page. "
                  "%1 to change the thumbnail size or the refresh mode. "
                  "The smart refresh mode only computes thumbnails "
                  "when the page data is present (displayed or cached.)"
                  "</html>").arg(mc) );
}


void 
QDjViewThumbnails::updateActions(void)
{
  QAction *action;
  int size = model->getSize();
  bool smart = model->getSmart();
  foreach(action, menu->actions())
    {
      QVariant data = action->data();
      if (data.type() == QVariant::Bool)
        action->setChecked(smart);
      else
        action->setChecked(data.toInt() == size);
    }
}


void 
QDjViewThumbnails::pageChanged(int pageno)
{
  if (pageno>=0 && pageno<djview->pageNum())
    {
      QModelIndex mi = model->index(pageno);
      if (! selection->isSelected(mi))
        selection->select(mi, QItemSelectionModel::ClearAndSelect);
      view->scrollTo(mi);
    }
}


void 
QDjViewThumbnails::activated(const QModelIndex &index)
{
  if (index.isValid())
    {
      int pageno = index.row();
      if (pageno>=0 && pageno<djview->pageNum())
        djview->goToPage(pageno);
    }
}


int 
QDjViewThumbnails::size()
{
  return model->getSize();
}


void 
QDjViewThumbnails::setSize(int size)
{
  model->setSize(size);
  updateActions();
}


void 
QDjViewThumbnails::setSize()
{
  QAction *action = qobject_cast<QAction*>(sender());
  if (action)
    setSize(action->data().toInt());
}


bool 
QDjViewThumbnails::smart()
{
  return model->getSmart();
}


void 
QDjViewThumbnails::setSmart(bool smart)
{
  model->setSmart(smart);
  updateActions();
}

void 
QDjViewThumbnails::contextMenuEvent(QContextMenuEvent *event)
{
  menu->exec(event->globalPos());
  event->accept();
}







// =======================================
// QDJVIEWFIND
// =======================================


class QDjViewFind::Model : public QAbstractListModel
{
  Q_OBJECT
public:
  Model(QDjViewFind*);
  // model stuff
public:
  virtual int rowCount(const QModelIndex &parent) const;
  virtual QVariant data(const QModelIndex &index, int role) const;
  void modelClear();
  int  modelFind(int pageno);
  bool modelSelect(int pageno);
  void modelAdd(int pageno, int hits);
private:
  struct RowInfo { int pageno; int hits; QString name; };
  QList<RowInfo> pages;
  // private data stuff
public:
  void nextHit(bool backwards);
  void startFind(bool backwards, int delay=0);
  void stopFind();
  typedef QList<miniexp_t> Hit;
  typedef QList<Hit> Hits;
  QMap<int, Hits> hits;
protected:
  virtual bool eventFilter(QObject*, QEvent*);
public slots:
  void documentClosed(QDjVuDocument*);
  void documentReady(QDjVuDocument*);
  void clear();
  void doHighlights(int pageno);
  void doPending();
  void workTimeout();
  void animTimeout();
  void makeSelectionVisible();
  void pageChanged(int);
  void textChanged();
  void pageinfo();
  void itemActivated(const QModelIndex&);
private:
  friend class QDjViewFind;
  QDjViewFind *widget;
  QDjView *djview;
  QTimer *animTimer;
  QTimer *workTimer;
  QItemSelectionModel *selection;
  QPushButton *animButton;
  QIcon animIcon;
  QIcon findIcon;
  QRegExp find;
  int curWork;
  int curPage;
  int curHit;
  int pending;
  bool searchBackwards;
  bool caseSensitive;
  bool wordOnly;
  bool working;
};


// ----------------------------------------
// QDJVIEWFIND::MODEL
// This class mixes the listview model
// and private data for qdjviewfind


QDjViewFind::Model::Model(QDjViewFind *widget)
  : QAbstractListModel(widget),
    widget(widget), 
    djview(widget->djview),
    selection(0),
    animButton(0),
    curPage(0),
    curHit(0),
    pending(0),
    searchBackwards(false),
    caseSensitive(false),
    wordOnly(true),
    working(false)
{
  selection = new QItemSelectionModel(this);
  animTimer = new QTimer(this);
  workTimer = new QTimer(this);
  workTimer->setSingleShot(true);
  connect(animTimer, SIGNAL(timeout()), this, SLOT(animTimeout()));
  connect(workTimer, SIGNAL(timeout()), this, SLOT(workTimeout()));
  findIcon = QIcon(":/images/icon_empty.png");
}


int 
QDjViewFind::Model::rowCount(const QModelIndex&) const
{
  return pages.size();
}


QVariant 
QDjViewFind::Model::data(const QModelIndex &index, int role) const
{
  if (index.isValid())
    {
      int row = index.row();
      if (row>=0 && row<pages.size())
        {
          const RowInfo &info = pages[row];
          switch(role)
            {
            case Qt::DisplayRole:
              return info.name;
            case Qt::ToolTipRole:
              if (info.hits <= 1)
                return tr("%1 hit").arg(info.hits);
              else
                return tr("%1 hits").arg(info.hits);
            default:
              break;
            }
        }
    }
  return QVariant();
}


void
QDjViewFind::Model::modelClear()
{
  int nrows = pages.size();
  if (nrows > 0)
    {
      beginRemoveRows(QModelIndex(), 0, nrows-1);
      pages.clear();
      endRemoveRows();
    }
}


int
QDjViewFind::Model::modelFind(int pageno)
{
  int lo = 0;
  int hi = pages.size();
  while (lo < hi)
    {
      int k = (lo + hi - 1) / 2;
      if (pageno > pages[k].pageno)
        lo = k + 1;
      else if (pageno < pages[k].pageno)
        hi = k;
      else
        lo = hi = k;
    }
  return lo;
}


bool
QDjViewFind::Model::modelSelect(int pageno)
{
  int lo = modelFind(pageno);
  QModelIndex mi = index(lo);
  if (lo < pages.size() && pages[lo].pageno == pageno)
    {
      if (!selection->isSelected(mi))
        selection->select(mi, QItemSelectionModel::ClearAndSelect);
      return true;
    }
  selection->select(mi, QItemSelectionModel::Clear);
  return false;
}


void
QDjViewFind::Model::modelAdd(int pageno, int hits)
{
  QString name = djview->pageName(pageno);
  RowInfo info;
  info.pageno = pageno;
  info.hits = hits;
  if (hits <= 1)
    info.name = tr("Page %1 (%2 hit)").arg(name).arg(hits);
  else
    info.name = tr("Page %1 (%2 hits)").arg(name).arg(hits);
  int lo = modelFind(pageno);
  if (lo < pages.size() && pages[lo].pageno == pageno)
    {
      pages[lo] = info;
      QModelIndex mi = index(lo);
      emit dataChanged(mi, mi);
    }
  else
    {
      beginInsertRows(QModelIndex(), lo, lo);
      pages.insert(lo, info);
      endInsertRows();
      if (pageno == djview->getDjVuWidget()->page())
        modelSelect(pageno);
    }
}


bool 
QDjViewFind::Model::eventFilter(QObject*, QEvent *event)
{
  switch (event->type())
    {
    case QEvent::Show:
      if (working)
        {
          animTimer->start();
          workTimer->start();
        }
    default:
      break;
    }
  return false;
}


void 
QDjViewFind::Model::documentClosed(QDjVuDocument *doc)
{
  disconnect(doc, 0, this, 0);
  stopFind();
  clear();
  curWork = 0;
  curPage = 0;
  curHit = 0;
  searchBackwards = false;
  widget->eraseText();
  widget->edit->setEnabled(false);
  widget->label->setText(QString());
  widget->stack->setCurrentIndex(0);
}


void 
QDjViewFind::Model::documentReady(QDjVuDocument *doc)
{
  clear();
  curWork = djview->getDjVuWidget()->page();
  curPage = curWork;
  curHit = -1;
  if (doc)
    {
      widget->edit->setEnabled(true);
      connect(doc, SIGNAL(pageinfo()), this, SLOT(pageinfo()));
      connect(doc, SIGNAL(idle()), this, SLOT(pageinfo()));
      if (! find.isEmpty())
        startFind(false);
    }
}


static bool
miniexp_get_int(miniexp_t &r, int &x)
{
  if (! miniexp_numberp(miniexp_car(r)))
    return false;
  x = miniexp_to_int(miniexp_car(r));
  r = miniexp_cdr(r);
  return true;
}


static bool
miniexp_get_rect(miniexp_t &r, QRect &rect)
{
  int x1,y1,x2,y2;
  if (! (miniexp_get_int(r, x1) && miniexp_get_int(r, y1) &&
         miniexp_get_int(r, x2) && miniexp_get_int(r, y2) ))
    return false;
  if (x2<x1 || y2<y1)
    return false;
  rect.setCoords(x1, y1, x2, y2);
  return true;
}


static bool
miniexp_get_text(miniexp_t exp, 
                 QString &result, QMap<int,miniexp_t> &positions,
                 QList<miniexp_t> nospace, QString &comma)
{
  miniexp_t type = miniexp_car(exp);
  miniexp_t r = exp = miniexp_cdr(exp);
  if (! miniexp_symbolp(type))
    return false;
  QRect rect;
  if (! miniexp_get_rect(r, rect))
    return false;
  miniexp_t s = miniexp_car(r);
  if (miniexp_stringp(s) && !miniexp_cdr(r))
    {
      result += comma;
      positions[result.size()] = exp;
      result += QString::fromUtf8(miniexp_to_str(s));
      if (comma.isEmpty() && !nospace.contains(type))
        comma = " ";
      return true;
    }
  while(miniexp_consp(s))
    {
      miniexp_get_text(s, result, positions, nospace, comma);
      r = miniexp_cdr(r);
      s = miniexp_car(r);
    }
  if (r) 
    return false;
  if (comma.isEmpty() && !nospace.contains(type))
    comma = " ";
  return true;
}


static QList<QList<miniexp_t> >
miniexp_search_text(miniexp_t exp, QRegExp regex)
{
  QList<QList<miniexp_t> > hits;
  QString text;
  QMap<int, miniexp_t> positions;
  // build string
  QList<miniexp_t> nospace;
  QString comma;
  nospace << miniexp_symbol("char");
  if (! miniexp_get_text(exp, text, positions, nospace, comma))
    return hits;
  // search hits
  int offset = 0;
  int match;
  while ((match = regex.indexIn(text, offset)) >= 0)
    {
      QList<miniexp_t> hit;
      QMap<int,miniexp_t>::const_iterator pos;
      int endmatch = match + regex.matchedLength();
      for(pos = positions.lowerBound(match);
          pos!=positions.end() && pos.key() < endmatch;
          ++pos)
        hit += pos.value();
      hits += hit;
      offset = endmatch;
    }
  return hits;
}


void 
QDjViewFind::Model::clear()
{
  QDjVuWidget *djvuWidget = djview->getDjVuWidget();
  QMap<int,Hits>::const_iterator pos;
  for (pos=hits.begin(); pos!=hits.end(); ++pos)
    if (pos.value().size() > 0)
      djvuWidget->clearHighlights(pos.key());
  hits.clear();
  modelClear();
}


void 
QDjViewFind::Model::doHighlights(int pageno)
{
  if (hits.contains(pageno))
    {
      QColor color = Qt::blue;
      QDjVuWidget *djvu = djview->getDjVuWidget();
      Hit pageHit;
      color.setAlpha(96);
      foreach(pageHit, hits[pageno])
        {
          QRect rect;
          miniexp_t exp;
          foreach(exp, pageHit)
            if (miniexp_get_rect(exp, rect))
              djvu->addHighlight(pageno, rect.x(), rect.y(),
                                 rect.width(), rect.height(),
                                 color );
        }
    }
}


void 
QDjViewFind::Model::doPending()
{
  QDjVuWidget *djvu = djview->getDjVuWidget();
  while (pending>0 && hits.contains(curPage) && pages.size()>0)
    {
      Hits pageHits = hits[curPage];
      int nhits = pageHits.size();
      if (searchBackwards)
        {
          if (curHit < 0 || curHit >= nhits)
            curHit = nhits;
          curHit--;
        }
      else
        {
          if (curHit < 0 || curHit >= nhits)
            curHit = -1;
          curHit++;
        }
      if (curHit >= 0 && curHit < nhits)
        {
          // jump to position
          pending --;
          Hit hit = pageHits[curHit];
          QRect rect;
          if (hit.size() > 0 && miniexp_get_rect(hit[0], rect))
            {
              QDjVuWidget::Position pos;
              pos.pageNo = curPage;
              pos.posPage = rect.center();
              pos.inPage = true;
              djvu->setPosition(pos, djvu->viewport()->rect().center());
            }
        }
      else
        {
          // next page
          curHit = -1;
          if (searchBackwards)
            {
              curPage -= 1;
              if (curPage < 0)
                curPage = djview->pageNum() - 1;
            }
          else
            {
              curPage += 1;
              if (curPage >= djview->pageNum())
                curPage = 0;
            }
        }
    }
  if (pending <= 0)
    djview->statusMessage(QString());
}


void 
QDjViewFind::Model::workTimeout()
{
  // do some work
  int startingPoint = curWork;
  bool somePagesWithText = false;
  doPending();
  while (working)
    {
      if (hits.contains(curWork))
        {
          somePagesWithText = true;
        }
      else
        {
          QString name = djview->pageName(curWork);
          QDjVuDocument *doc = djview->getDocument();
          miniexp_t exp = doc->getPageText(curWork, false);
          if (exp == miniexp_dummy)
            {
              // data not present
              if (pending>0)
                djview->statusMessage(tr("Searching page %1.").arg(name));
              if (pending>0 || widget->isVisible())
                doc->getPageText(curWork, true);                
              // timer will be reactivated by pageinfo()
              return;
            }
          if (exp != miniexp_nil)
            {
              somePagesWithText = true;
              if (pending > 0)
                djview->statusMessage(tr("Searching page %1.").arg(name));
              Hits pageHits = miniexp_search_text(exp, find);
              hits[curWork] = pageHits;
              if (pageHits.size() > 0)
                {
                  modelAdd(curWork, pageHits.size());
                  doHighlights(curWork);
                  doPending();
                  makeSelectionVisible();
                }
              // enough
              break;
            }
        }
      // next page
      int pageNum = djview->pageNum();
      if (searchBackwards)
        {
          if (curWork <= 0)
            curWork = pageNum;
          curWork -= 1;
        }
      else
        {
          curWork += 1;
          if (curWork >= pageNum)
            curWork = 0;
        }
      // finished?
      if (curWork == startingPoint)
        {
          stopFind();
          djview->statusMessage(QString());
          if (! pages.size())
            {
              QString msg = tr("No hits!");
              if (! somePagesWithText)
                {
                  widget->eraseText();
                  widget->edit->setEnabled(false);
                  msg = tr("<html>Document is not searchable. "
                           "No page contains information "
                           "about its textual content.</html>");
                }
              widget->stack->setCurrentWidget(widget->label);
              widget->label->setText(msg);
            }
        }
    }
  // restart timer
  if (working)
    workTimer->start(0);
}


void 
QDjViewFind::Model::animTimeout()
{
  if (animButton && !animIcon.isNull())
    {
      if (animButton->icon().serialNumber() == findIcon.serialNumber())
        animButton->setIcon(animIcon);
      else
        animButton->setIcon(findIcon);
    }
}


void 
QDjViewFind::Model::nextHit(bool backwards)
{
  if (working && backwards != searchBackwards)
    {
      startFind(backwards);
    }
  else if (! find.isEmpty())
    {
      searchBackwards = backwards;
      pending = qMin(5, pending+1);
      doPending();
      if (working && pending>0 && !workTimer->isActive())
        workTimer->start(0);
    }
}


void 
QDjViewFind::Model::startFind(bool backwards, int delay)
{
  stopFind();
  searchBackwards = backwards;
  if (! find.isEmpty() && djview->pageNum() > 0)
    {
      widget->label->setText(QString());
      widget->stack->setCurrentIndex(0);
      animButton = (backwards) ? widget->upButton : widget->downButton;
      animIcon = animButton->icon();
      animTimer->start(250);
      workTimer->start(delay);
      curWork = djview->getDjVuWidget()->page();
      working = true;
      pending = 1;
    }
}


void 
QDjViewFind::Model::stopFind()
{
  animTimer->stop();
  workTimer->stop();
  if (animButton)
    {
      animButton->setIcon(animIcon);
      animButton = 0;
    }
  working = false;
  pending = 0;
}


void 
QDjViewFind::Model::pageinfo()
{
  if (working && !workTimer->isActive())
    workTimer->start(0);
}


void 
QDjViewFind::Model::makeSelectionVisible()
{
  QModelIndexList s = selection->selectedIndexes();
  if (s.size() > 0)
    widget->view->scrollTo(s[0]);
}


void 
QDjViewFind::Model::pageChanged(int pageno)
{
  if (pageno != curPage)
    curHit = -1;
  curPage = curWork = pageno;
  pending = 0;
}


void 
QDjViewFind::Model::textChanged()
{
  stopFind();
  clear();
  QString s = widget->text();
  widget->label->setText(QString());
  widget->stack->setCurrentIndex(0);
  if (s.isEmpty())
    {
      find = QRegExp();
    }
  else
    {
      s = QRegExp::escape(widget->text());
      s.replace(QRegExp("\\s+"), " ");
      if (wordOnly)
        s = "\\b" + s + "\\b";
      find = QRegExp(s);
      if (caseSensitive)
        find.setCaseSensitivity(Qt::CaseSensitive);
      else
        find.setCaseSensitivity(Qt::CaseInsensitive);
      startFind(searchBackwards, 250);
    }
}


void 
QDjViewFind::Model::itemActivated(const QModelIndex &mi)
{
  if (mi.isValid())
    {
      int row = mi.row();
      if (row>=0 && row<pages.size())
        djview->goToPage(pages[row].pageno);
    }
}



// ----------------------------------------
// QDJVIEWFIND


QDjViewFind::QDjViewFind(QDjView *djview)
  : QWidget(djview), 
    djview(djview), 
    model(0),
    view(0)
{
  model = new Model(this);
  installEventFilter(model);

  view = new QListView(this);
  view->setModel(model);
  view->setSelectionModel(model->selection);
  view->setDragEnabled(false);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setTextElideMode(Qt::ElideMiddle);
  view->setViewMode(QListView::ListMode);
  view->setWrapping(false);
  view->setResizeMode(QListView::Adjust);
  
  QAction *eraseAction = new QAction(tr("Erase text"), this);
  eraseAction->setIcon(QIcon(":/images/icon_erase.png"));
  QAction *caseSensitiveAction = new QAction(tr("Case sensitive"), this);
  caseSensitiveAction->setCheckable(true);
  caseSensitiveAction->setChecked(model->caseSensitive);
  QAction *wordOnlyAction = new QAction(tr("Words only"), this);
  wordOnlyAction->setCheckable(true);
  wordOnlyAction->setChecked(model->wordOnly);
  menu = new QMenu(this);
  menu->addAction(caseSensitiveAction);
  menu->addAction(wordOnlyAction);
  QBoxLayout *vlayout = new QVBoxLayout(this);
  QToolBar *tools = new QToolBar(this);
  tools->addAction(eraseAction);
  edit = new QLineEdit(tools);
  tools->addWidget(edit);
  tools->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  vlayout->addWidget(tools);
  QBoxLayout *hlayout = new QHBoxLayout;
  vlayout->addLayout(hlayout);
  upButton = new QPushButton(this);
  upButton->setIcon(QIcon(":/images/icon_up.png"));
  upButton->setToolTip(tr("Find Previous (Shift+F3) "));
  hlayout->addWidget(upButton);
  downButton = new QPushButton(this);
  downButton->setIcon(QIcon(":/images/icon_down.png"));
  downButton->setToolTip(tr("Find Next (F3) "));
  hlayout->addWidget(downButton);
  hlayout->addStretch(2);
  QPushButton *optionButton = new QPushButton(tr("Options"), this);
  optionButton->setMenu(menu);
  optionButton->setFlat(true);
  optionButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
  hlayout->addWidget(optionButton);
  stack = new QStackedLayout();
  view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  view->setFrameShadow(QFrame::Sunken);
  view->setFrameShape(QFrame::StyledPanel);
  stack->addWidget(view);
  label = new QLabel(this);
  label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);
  label->setFrameShadow(QFrame::Sunken);
  label->setFrameShape(QFrame::StyledPanel);
  stack->addWidget(label);
  vlayout->addLayout(stack);
  
  connect(djview, SIGNAL(documentClosed(QDjVuDocument*)),
          model, SLOT(documentClosed(QDjVuDocument*)) );
  connect(djview, SIGNAL(documentReady(QDjVuDocument*)),
          model, SLOT(documentReady(QDjVuDocument*)) );
  connect(view, SIGNAL(activated(const QModelIndex&)),
          model, SLOT(itemActivated(const QModelIndex&)));
  connect(djview->getDjVuWidget(), SIGNAL(pageChanged(int)),
          this, SLOT(pageChanged(int)));
  connect(edit, SIGNAL(textChanged(QString)),
          model, SLOT(textChanged()));
  connect(edit, SIGNAL(returnPressed()),
          this, SLOT(findAgain()));
  connect(eraseAction, SIGNAL(triggered()), 
          this, SLOT(eraseText()));
  connect(caseSensitiveAction, SIGNAL(triggered(bool)),
          this, SLOT(setCaseSensitive(bool)));
  connect(wordOnlyAction, SIGNAL(triggered(bool)),
          this, SLOT(setWordOnly(bool)));
  connect(upButton, SIGNAL(clicked()), 
          this, SLOT(findPrev()));
  connect(downButton, SIGNAL(clicked()), 
          this, SLOT(findNext()));
  
  if (djview->getDocument())
    model->documentReady(djview->getDocument());
}


void 
QDjViewFind::contextMenuEvent(QContextMenuEvent *event)
{
  menu->exec(event->globalPos());
  event->accept();
}


void
QDjViewFind::takeFocus(Qt::FocusReason reason)
{
  if (edit->isVisible())
    edit->setFocus(reason);
}


QString 
QDjViewFind::text()
{
  return edit->text();
}


bool 
QDjViewFind::caseSensitive()
{
  return model->caseSensitive;
}


bool 
QDjViewFind::wordOnly()
{
  return model->wordOnly;
}


void 
QDjViewFind::setText(QString s)
{
  if (s != text())
    edit->setText(s);
}


void 
QDjViewFind::eraseText()
{
  setText(QString());
}


void 
QDjViewFind::setCaseSensitive(bool b)
{
  if (b != model->caseSensitive)
    {
      model->caseSensitive = b;
      model->textChanged();
    }
}


void 
QDjViewFind::setWordOnly(bool b)
{
  if (b != model->wordOnly)
    {
      model->wordOnly = b;
      model->textChanged();
    }
}


void 
QDjViewFind::findNext()
{
  if (text().isEmpty())
    djview->showSideBar("find");
  model->nextHit(false);
}


void 
QDjViewFind::findPrev()
{
  if (text().isEmpty())
    djview->find();
  model->nextHit(true);
}


void 
QDjViewFind::findAgain()
{
  if (model->searchBackwards)
    findPrev();
  else
    findNext();
}


void 
QDjViewFind::pageChanged(int pageno)
{
  if (pageno>=0 && pageno<djview->pageNum())
    {
      model->pageChanged(pageno);
      if (model->modelSelect(pageno))
        model->makeSelectionVisible();
    }
}








// ----------------------------------------
// MOC

#include "qdjviewsidebar.moc"




/* -------------------------------------------------------------
   Local Variables:
   c++-font-lock-extra-types: ( "\\sw+_t" "[A-Z]\\sw*[a-z]\\sw*" )
   End:
   ------------------------------------------------------------- */
