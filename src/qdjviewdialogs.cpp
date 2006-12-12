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
#include <errno.h>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QMessageBox>
#include <QObject>
#include <QPainter>
#include <QPrinter>
#include <QPrintDialog>
#include <QPushButton>
#include <QRegExp>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtAlgorithms>

#include <libdjvu/miniexp.h>
#include <libdjvu/ddjvuapi.h>

#include "qdjview.h"
#include "qdjviewprefs.h"
#include "qdjviewdialogs.h"
#include "qdjvuwidget.h"
#include "qdjvu.h"


#if DDJVUAPI_VERSION < 18
# error "DDJVUAPI_VERSION>=18 is required !"
#endif


// =======================================
// QDJVIEWERRORDIALOG
// =======================================



#include "ui_qdjviewerrordialog.h"

struct QDjViewErrorDialog::Private {
  Ui::QDjViewErrorDialog ui;
  QList<QString> messages;
  void compose();
};

QDjViewErrorDialog::~QDjViewErrorDialog()
{
  delete d;
}

QDjViewErrorDialog::QDjViewErrorDialog(QWidget *parent)
  : QDialog(parent), 
    d(new Private)
{
  d->ui.setupUi(this);
  connect(d->ui.okButton, SIGNAL(clicked()), this, SLOT(accept()));
  d->ui.textEdit->viewport()->setBackgroundRole(QPalette::Background);
  setWindowTitle(tr("DjView Error"));
}


void
QDjViewErrorDialog::compose()
{
  QString html;
  for (int i=0; i<d->messages.size(); i++)
    html = "<li>" + d->messages[i] + "</li>" + html;
  d->ui.textEdit->setHtml("<html><ul>" + html + "</ul></html>");
  QScrollBar *scrollBar = d->ui.textEdit->verticalScrollBar();
  scrollBar->setValue(scrollBar->maximum());
}

void 
QDjViewErrorDialog::error(QString msg, QString, int)
{
  // Remove [1-nnnnn] prefix from djvulibre-3.5
  if (msg.startsWith("["))
    msg = msg.replace(QRegExp("^\\[\\d*-?\\d*\\]\\s*") , "");
  // Ignore empty and duplicate messages
  if (msg.isEmpty()) return;
  if (!d->messages.isEmpty() && msg == d->messages[0]) return;
  // Add message
  d->messages.prepend(msg);
  while (d->messages.size() >= 16)
    d->messages.removeLast();
  compose();
}

void 
QDjViewErrorDialog::prepare(QMessageBox::Icon icon, QString caption)
{
  if (icon != QMessageBox::NoIcon)
    d->ui.iconLabel->setPixmap(QMessageBox::standardIcon(icon));
  if (caption.isEmpty())
    caption = tr("Error - DjView", "dialog caption");
  setWindowTitle(caption);
}

void 
QDjViewErrorDialog::done(int result)
{
  emit closing();
  d->messages.clear();
  QDialog::done(result);
}




// =======================================
// QDJVIEWINFODIALOG
// =======================================


#include "ui_qdjviewinfodialog.h"

struct QDjViewInfoDialog::Private {
  Ui::QDjViewInfoDialog ui;
  QDjView *djview;
  QDjVuDocument *document;
  QList<ddjvu_fileinfo_t> files;
  int fileno;
  int pageno;
  bool done;
};


QDjViewInfoDialog::~QDjViewInfoDialog()
{
  delete d;
}


QDjViewInfoDialog::QDjViewInfoDialog(QDjView *parent)
  : QDialog(parent), d(new Private)
{
  d->djview = parent;
  d->document = 0;
  d->fileno = 0;
  d->pageno = -1;
  d->done = false;
  d->ui.setupUi(this);

  QDjViewPrefs *prefs = QDjViewPrefs::instance();
  int index = prefs->infoDialogTab;
  if (index >= 0 && index < d->ui.tabWidget->count())
    d->ui.tabWidget->setCurrentIndex(index);
  
  QFont font = d->ui.fileText->font();
  font.setFixedPitch(true);
  font.setFamily("monospace");
  font.setPointSize((font.pointSize() * 5 + 5) / 6);
  d->ui.fileText->setFont(font);
  d->ui.fileText->viewport()->setBackgroundRole(QPalette::Background);
  
  QStringList labels;
  d->ui.docTable->setColumnCount(6);
  labels << tr("File #") << tr("File Name") 
         << tr("File Size") << tr("File Type") 
         << tr("Page #") << tr("Page Title");
  d->ui.docTable->setHorizontalHeaderLabels(labels);
  d->ui.docTable->verticalHeader()->hide();
  d->ui.docTable->horizontalHeader()->setHighlightSections(false);
  d->ui.docTable->horizontalHeader()->setStretchLastSection(true);

  connect(d->djview, SIGNAL(documentClosed(QDjVuDocument*)), 
          this, SLOT(clear()));
  connect(d->djview, SIGNAL(documentReady(QDjVuDocument*)), 
          this, SLOT(refresh()));
  connect(d->djview->getDjVuWidget(), SIGNAL(pageChanged(int)),
          this, SLOT(setPage(int)));
  connect(d->ui.okButton, SIGNAL(clicked()), 
          this, SLOT(accept()) );
  connect(d->ui.nextButton, SIGNAL(clicked()), 
          this, SLOT(nextFile()) );
  connect(d->ui.jumpButton, SIGNAL(clicked()), 
          this, SLOT(jumpToSelectedPage()) );
  connect(d->ui.fileCombo, SIGNAL(activated(int)), 
          this, SLOT(setFile(int)) );
  connect(d->ui.prevButton, SIGNAL(clicked()), 
          this, SLOT(prevFile()) );
  connect(d->ui.docTable, SIGNAL(currentCellChanged(int,int,int,int)), 
          this, SLOT(setFile(int)) );
  connect(d->ui.docTable, SIGNAL(itemDoubleClicked(QTableWidgetItem*)),
          this, SLOT(jumpToSelectedPage()) );
  
  d->ui.fileCombo->setEnabled(false);
  d->ui.jumpButton->setEnabled(false);
  d->ui.prevButton->setEnabled(false);
  d->ui.nextButton->setEnabled(false);

  // what's this
  QWidget *wd = d->ui.tabDocument;
  wd->setWhatsThis(tr("<html><b>Document information</b><br>"
                      "This panel shows information about the document and "
                      "its component files. Select a component file "
                      "to display detailled information in the 'File' "
                      "tab. Double click a component file to show "
                      "the corresponding page in the main window."
                      "</html>"));
  QWidget *wf = d->ui.tabFile;
  wf->setWhatsThis(tr("<html><b>File and page information</b><br>"
                      "This panel shows the structure of the DjVu data "
                      "corresponding to the component file or the page "
                      "selected in the 'Document' tab. The arrow buttons "
                      "let you navigate to the previous or next "
                      "component file.</html>"));
}

void 
QDjViewInfoDialog::refresh()
{
  // document known
  if (! d->document)
    {
      QDjVuDocument *doc;
      doc = d->djview->getDjVuWidget()->document();
      if (! doc)
        return;
      d->document = doc;
      connect(doc, SIGNAL(pageinfo()),
              this, SLOT(refresh()) );
    }
  // document ready
  if (! d->files.size())
    {
      QDjVuDocument *doc = d->document;
      if (ddjvu_document_decoding_status(*doc) != DDJVU_JOB_OK)
        return;
      int filenum = ddjvu_document_get_filenum(*doc);
      for (int i=0; i<filenum; i++)
        {
          ddjvu_fileinfo_t info;
          ddjvu_document_get_fileinfo(*doc, i, &info);
          d->files << info;
        }
      fillFileCombo();
      fillDocLabel();
      fillDocTable();
      if (d->pageno >= 0)
        setPage(d->pageno);
      else if (d->fileno >= 0)
        setFile(d->fileno);
      d->ui.fileCombo->setEnabled(true);
      d->done = false;
    }
  
  // file ready
  if (! d->done)
    {
      QDjVuDocument *doc = d->document;
      ddjvu_fileinfo_t &info = d->files[d->fileno];
      d->done = true;

      // file dump
      QString dump = tr("Waiting for data...");
      char *s = ddjvu_document_get_filedump(*doc, d->fileno);
      if (! s)
        d->done = false;
      else
        {
          dump = QString::fromUtf8(s);
          free(s);
        }
      d->ui.fileText->setPlainText(dump);
      d->ui.prevButton->setEnabled(d->fileno - 1 >= 0);
      d->ui.nextButton->setEnabled(d->fileno + 1 < d->files.size());
      d->ui.jumpButton->setEnabled(info.type == 'P');
      d->ui.fileCombo->setCurrentIndex(d->fileno);
      QTableWidget *table = d->ui.docTable;
      int rows = table->rowCount();
      int cols = table->columnCount();
      QTableWidgetSelectionRange all(0,0,rows-1,cols-1);
      table->setRangeSelected(all, false);
      table->selectRow(d->fileno);
    }
}

void 
QDjViewInfoDialog::setPage(int pageno)
{
  if (d->document && d->files.size())
    {
      int i;
      for (i=0; i<d->files.size(); i++)
        if (d->files[i].pageno == pageno && 
            d->files[i].type == 'P' )
          break;
      if (i < d->files.size())
        setFile(i);
    }
  else
    {
      d->fileno = -1;
      d->pageno = pageno;
    }
}

void 
QDjViewInfoDialog::setFile(int fileno)
{
  if (d->document && d->files.size())
    {
      if (fileno >= 0 && 
          fileno < d->files.size() &&
          fileno != d->fileno )
        {
          d->fileno = fileno;
          d->pageno = d->files[fileno].pageno;
          d->done = false;
          refresh();
        }
    }
  else
    {
      d->pageno = -1;
      d->fileno = fileno;
    }
}

void 
QDjViewInfoDialog::prevFile()
{
  if (d->document && d->files.size())
    if (d->fileno - 1 >= 0)
      setFile(d->fileno - 1);
}

void 
QDjViewInfoDialog::nextFile()
{
  if (d->document && d->files.size())
    if (d->fileno + 1 < d->files.size())
      setFile(d->fileno + 1);
}

void 
QDjViewInfoDialog::jumpToSelectedPage()
{
  if (d->document && d->files.size())
    {
      ddjvu_fileinfo_t &info = d->files[d->fileno];
      if (info.type == 'P')
        d->djview->goToPage(info.pageno);
    }
}

void 
QDjViewInfoDialog::clear()
{
  hide();
  if (d->document)
    disconnect(d->document, 0, this, 0);
  d->files.clear();
  d->document = 0;
  QDjViewPrefs *prefs = QDjViewPrefs::instance();
  prefs->infoDialogTab = d->ui.tabWidget->currentIndex();
}

void 
QDjViewInfoDialog::fillFileCombo()
{
  QComboBox *fileCombo = d->ui.fileCombo;
  fileCombo->clear();
  for (int fileno=0; fileno<d->files.size(); fileno++)
    {
      ddjvu_fileinfo_t &info = d->files[fileno];
      QString msg;
      if (info.type == 'P')
        {
          if (info.title && info.name && strcmp(info.title, info.name))
            msg = tr("Page #%1 - \253 %2 \273") // << .. >>
              .arg(info.pageno + 1).arg(QString::fromUtf8(info.title));
          else
            msg = tr("Page #%1").arg(info.pageno + 1);
        }
      else if (info.type == 'T')
        msg = tr("Thumbnails");
      else if (info.type == 'S')
        msg = tr("Shared annotations");
      else
        msg = tr("Shared data");
      fileCombo->addItem(tr("File #%1 - ").arg(fileno+1) + msg);
    }
}

void 
QDjViewInfoDialog::fillDocLabel()
{
  if (d->document && d->files.size())
    {
      QStringList msg;
      QDjVuDocument *doc = d->document;
      ddjvu_document_type_t docType = ddjvu_document_get_type(*doc);
      if (docType == DDJVU_DOCTYPE_SINGLEPAGE)
        msg << tr("Single DjVu page");
      else 
        {
          if (docType == DDJVU_DOCTYPE_BUNDLED)
            msg << tr("Bundled DjVu document");
          else if (docType == DDJVU_DOCTYPE_INDIRECT)
            msg << tr("Indirect DjVu document");
          else if (docType == DDJVU_DOCTYPE_OLD_BUNDLED)
            msg << tr("Obsolete bundled DjVu document");
          else if (docType == DDJVU_DOCTYPE_OLD_INDEXED)
            msg << tr("Obsolete indexed DjVu document");
          
          int pagenum = ddjvu_document_get_pagenum(*doc);
          int filenum = ddjvu_document_get_filenum(*doc);
          msg << tr("%1 files").arg(filenum);
          msg << tr("%1 pages").arg(pagenum);
        }
      d->ui.docLabel->setText(msg.join(" - "));
    }
}

static void
setTableWhatsThis(QTableWidget *table, QString text)
{
  for (int i=0; i<table->rowCount(); i++)
    for (int j=0; j<table->columnCount(); j++)
      table->item(i,j)->setWhatsThis(text);
}


void 
QDjViewInfoDialog::fillDocTable()
{
  int filenum = d->files.size();
  QTableWidget *table = d->ui.docTable;
  table->setRowCount(filenum);
  for (int i=0; i<filenum; i++)
    fillDocRow(i);
#if QT_VERSION >= 0x040100
  table->resizeColumnsToContents();
  table->resizeRowsToContents();
#endif
  table->horizontalHeader()->setStretchLastSection(true);
  setTableWhatsThis(table, d->ui.tabDocument->whatsThis());
}

void 
QDjViewInfoDialog::fillDocRow(int i)
{
  ddjvu_fileinfo_t &info = d->files[i];
  QTableWidget *table = d->ui.docTable;

  QTableWidgetItem *numItem = new QTableWidgetItem();
  numItem->setText(QString(" %1 ").arg(i+1));
  numItem->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
  numItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
  table->setItem(i, 0, numItem);

  QTableWidgetItem *nameItem = new QTableWidgetItem();
  QString name = (info.name) ? QString::fromUtf8(info.name) : tr("n/a");
  nameItem->setText(QString(" %1 ").arg(name));
  nameItem->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
  nameItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
  table->setItem(i, 1, nameItem);

  QTableWidgetItem *sizeItem = new QTableWidgetItem();
  sizeItem->setText((info.size>0) ? QString(" %1 ").arg(info.size) : tr("n/a"));
  sizeItem->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
  sizeItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
  table->setItem(i, 2, sizeItem);

  QTableWidgetItem *typeItem = new QTableWidgetItem();
  if (info.type == 'P')
    typeItem->setText(tr(" Page "));
  else if (info.type == 'T')
    typeItem->setText(tr(" Thumbnails "));
  else
    typeItem->setText(tr(" Shared "));
  typeItem->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
  typeItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
  table->setItem(i, 3, typeItem);
  
  QTableWidgetItem *pnumItem = new QTableWidgetItem();
  if (info.type == 'P')
    pnumItem->setText(QString(" %1 ").arg(info.pageno+1));
  pnumItem->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
  pnumItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
  table->setItem(i, 4, pnumItem);
  
  QTableWidgetItem *titleItem = new QTableWidgetItem();
  if (info.type == 'P')
    {
      QString title = d->djview->pageName(info.pageno, true);
      titleItem->setText(QString(" %1 ").arg(title));
    }
  titleItem->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
  titleItem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
  table->setItem(i, 5, titleItem);
}





// =======================================
// QDJVIEWMETADIALOG
// =======================================


#include "ui_qdjviewmetadialog.h"

struct QDjViewMetaDialog::Private {
  Ui::QDjViewMetaDialog ui;
  QDjView *djview;
  QDjVuDocument *document;
  int pageno;
  minivar_t docAnno;
  minivar_t pageAnno;
};


QDjViewMetaDialog::~QDjViewMetaDialog()
{
  delete d;
}


QDjViewMetaDialog::QDjViewMetaDialog(QDjView *parent)
  : QDialog(parent), d(new Private)
{
  d->djview = parent;
  d->document = 0;
  d->pageno = 0;
  d->docAnno = miniexp_dummy;
  d->pageAnno = miniexp_dummy;
  d->ui.setupUi(this);
  
  // get preferences
  QDjViewPrefs *prefs = QDjViewPrefs::instance();
  int index = prefs->metaDialogTab;
  if (index >= 0 && index < d->ui.tabWidget->count())
    d->ui.tabWidget->setCurrentIndex(index);

  // make ctrl+c work
  new QShortcut(QKeySequence(tr("Ctrl+C","copy")), this, SLOT(copy()));
  // tweaks
  QStringList labels;
  labels << tr(" Key ") << tr(" Value ");
  d->ui.docTable->setColumnCount(2);
  d->ui.docTable->setHorizontalHeaderLabels(labels);
  d->ui.docTable->horizontalHeader()->setHighlightSections(false);
  d->ui.docTable->horizontalHeader()->setStretchLastSection(true);
  d->ui.docTable->verticalHeader()->hide();
  d->ui.pageTable->setColumnCount(2);
  d->ui.pageTable->setHorizontalHeaderLabels(labels);
  d->ui.pageTable->horizontalHeader()->setHighlightSections(false);
  d->ui.pageTable->horizontalHeader()->setStretchLastSection(true);
  d->ui.pageTable->verticalHeader()->hide();
  d->ui.pageCombo->setEnabled(false);
  d->ui.jumpButton->setEnabled(false);
  d->ui.prevButton->setEnabled(false);
  d->ui.nextButton->setEnabled(false);
  // connections
  connect(d->djview, SIGNAL(documentClosed(QDjVuDocument*)),
          this, SLOT(clear()) );
  connect(d->djview, SIGNAL(documentReady(QDjVuDocument*)), 
          this, SLOT(refresh()));
  connect(d->djview->getDjVuWidget(), SIGNAL(pageChanged(int)),
          this, SLOT(setPage(int)) );
  connect(d->ui.okButton, SIGNAL(clicked()),
          this, SLOT(accept()) );
  connect(d->ui.jumpButton, SIGNAL(clicked()),
          this, SLOT(jumpToSelectedPage()) );
  connect(d->ui.pageCombo, SIGNAL(activated(int)),
          this, SLOT(setPage(int)) );
  connect(d->ui.prevButton, SIGNAL(clicked()),
          this, SLOT(prevPage()) );
  connect(d->ui.nextButton, SIGNAL(clicked()),
          this, SLOT(nextPage()) );
  connect(d->ui.pageCombo, SIGNAL(activated(int)),
          this, SLOT(setPage(int)) );
  // what's this
  QWidget *wd = d->ui.docTab;
  wd->setWhatsThis(tr("<html><b>Document metadata</b><br>"
                      "This panel displays metadata pertaining "
                      "to the document, such as author, title, "
                      "references, etc. "
                      "This information can be saved into the document "
                      "with program <tt>djvused</tt>: use the commands "
                      "<tt>create-shared-ant</tt> and <tt>set-meta</tt>."
                      "</html>"));
  QWidget *wp = d->ui.pageTab;
  wp->setWhatsThis(tr("<html><b>Page metadata</b><br>"
                      "This panel displays metadata pertaining "
                      "to a specific page. "
                      "Page specific metadata override document metadata. "
                      "This information can be saved into the document "
                      "with program <tt>djvused</tt>: use command "
                      "<tt>select</tt> to select the page and command "
                      "<tt>set-meta</tt> to specify the metadata entries."
                      "</html>"));
}

static QMap<QString,QString>
metadataFromAnnotations(miniexp_t p)
{
  QMap<QString,QString> m;
  miniexp_t s_metadata = miniexp_symbol("metadata");
  while (miniexp_consp(p))
    {
      if (miniexp_caar(p) == s_metadata)
        {
          miniexp_t q = miniexp_cdar(p);
          while (miniexp_consp(q))
            {
              miniexp_t a = miniexp_car(q);
              q = miniexp_cdr(q);
              if (miniexp_consp(a) && 
                  miniexp_symbolp(miniexp_car(a)) &&
                  miniexp_stringp(miniexp_cadr(a)) )
                {
                  QString k;
                  k = QString::fromUtf8(miniexp_to_name(miniexp_car(a)));
                  m[k] = QString::fromUtf8(miniexp_to_str(miniexp_cadr(a)));
                }
            }
        }
      p = miniexp_cdr(p);
    }
  return m;
}

static void
metadataSubtract(QMap<QString,QString> &from, QMap<QString,QString> m)
{
  QMap<QString,QString>::const_iterator i = m.constBegin();
  for( ; i != m.constEnd(); i++)
    if (from.contains(i.key()) && from[i.key()] == i.value())
      from.remove(i.key());
}

static void
metadataFill(QTableWidget *table, QMap<QString,QString> m)
{
  QList<QString> keys;
  QMap<QString,QString>::const_iterator i = m.constBegin();
  for( ; i != m.constEnd(); i++)
    keys << i.key();
  qSort(keys.begin(), keys.end());
  int nkeys = keys.size();
  table->setRowCount(nkeys);
  for(int j = 0; j < nkeys; j++)
    {
      QTableWidgetItem *kitem = new QTableWidgetItem(keys[j]);
      QTableWidgetItem *vitem = new QTableWidgetItem(m[keys[j]]);
      kitem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
      vitem->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
      table->setItem(j, 0, kitem);
      table->setItem(j, 1, vitem);
    }
#if QT_VERSION >= 0x040100
  table->resizeColumnsToContents();
  table->resizeRowsToContents();
#endif
  table->horizontalHeader()->setStretchLastSection(true);
}

void 
QDjViewMetaDialog::refresh()
{
  // new document?
  if (! d->document)
    {
      QDjVuDocument *doc;
      doc = d->djview->getDjVuWidget()->document();
      if (! doc)
        return;
      d->document = doc;
      connect(doc, SIGNAL(pageinfo()),
              this, SLOT(refresh()) );
    }
  // document ready
  if (! d->ui.pageCombo->count())
    {
      if (! d->djview->pageNum())
        return;
      d->djview->fillPageCombo(d->ui.pageCombo);
      d->ui.pageCombo->setEnabled(true);
      d->ui.jumpButton->setEnabled(true);
    }
  // document annotations known
  if (d->docAnno == miniexp_dummy)
    {
      d->docAnno = d->document->getDocumentAnnotations();
      if (d->docAnno != miniexp_dummy)
        {
          QMap<QString,QString> docMeta = metadataFromAnnotations(d->docAnno);
          metadataFill(d->ui.docTable, docMeta);
          setTableWhatsThis(d->ui.docTable, d->ui.docTab->whatsThis());
        }
    }
  // page annotations
  if (d->ui.pageCombo->count() > 0)
    {
      d->pageno = qBound(0, d->pageno, d->djview->pageNum()-1);
      d->ui.prevButton->setEnabled(d->pageno-1 >= 0);
      d->ui.nextButton->setEnabled(d->pageno+1 < d->djview->pageNum());
      d->ui.pageCombo->setCurrentIndex(d->pageno);
      if (d->document && d->pageAnno == miniexp_dummy)
        {
          d->pageAnno = d->document->getPageAnnotations(d->pageno);
          if (d->pageAnno != miniexp_dummy)
            {
              QMap<QString,QString> docMeta 
                = metadataFromAnnotations(d->docAnno);
              QMap<QString,QString> pageMeta 
                = metadataFromAnnotations(d->pageAnno);
              metadataSubtract(pageMeta, docMeta);
              metadataFill(d->ui.pageTable, pageMeta);
              setTableWhatsThis(d->ui.pageTable, d->ui.pageTab->whatsThis());
            }
        }
    }
}

void 
QDjViewMetaDialog::prevPage()
{
  setPage(qMax(d->pageno - 1, 0));
}

void 
QDjViewMetaDialog::nextPage()
{
  setPage(qMin(d->pageno + 1, d->djview->pageNum() - 1));
}

void 
QDjViewMetaDialog::setPage(int pageno)
{
  if (d->document && pageno != d->pageno)
    {
      d->pageno = pageno;
      d->pageAnno = miniexp_dummy;
      d->ui.pageTable->setRowCount(0);
      refresh();
    }
}

void 
QDjViewMetaDialog::jumpToSelectedPage()
{
  if (d->document &&
      d->pageno >= 0 && d->pageno < d->djview->pageNum() )
    d->djview->goToPage(d->pageno);
}

void 
QDjViewMetaDialog::clear()
{
  hide();
  if (d->document)
    disconnect(d->document, 0, this, 0);
  d->document = 0;
  d->pageno = 0;
  d->docAnno = miniexp_dummy;
  d->pageAnno = miniexp_dummy;
  d->ui.pageCombo->clear();
  d->ui.pageCombo->setEnabled(false);
  d->ui.docTable->setRowCount(0);
  d->ui.pageTable->setRowCount(0);
  d->ui.jumpButton->setEnabled(false);
  QDjViewPrefs *prefs = QDjViewPrefs::instance();
  prefs->infoDialogTab = d->ui.tabWidget->currentIndex();
}

void 
QDjViewMetaDialog::copy()
{
  QTableWidget *table = d->ui.pageTable;
  if (d->ui.tabWidget->currentWidget() == d->ui.docTab)
    table = d->ui.docTable;
  QList<QTableWidgetItem*> selected = table->selectedItems();
  if (selected.size() == 1)
    QApplication::clipboard()->setText(selected[0]->text());
}



// =======================================
// QDJVIEWEXPORTER
// =======================================



class QDjViewExporter : public QObject
{
  Q_OBJECT
public:
  QDjViewExporter(QDialog *parent, QDjView *djview);
  virtual bool canExportOnePage()         { return true; }
  virtual bool canExportPageRange()       { return true; }
  virtual void setFileName(QString s)     { fileName = s; }
  virtual void setPrinter(QPrinter*)      { }
  virtual void setPageRange(int f, int t) { fromPage = f; toPage = t; }
  virtual void setErrorCaption(QString s) { errorCaption = s; }
  virtual int      propertyPages()        { return 0; }
  virtual QWidget* propertyPage(int)      { return 0; }
  virtual ddjvu_status_t status();
public slots:
  virtual void run() = 0;
  virtual void stop();
  virtual void error(QString, QString, int);
signals:
  void progress(int i);
protected:
  QDialog            *parent;
  QDjView            *djview;
  QDjViewErrorDialog *errorDialog;
  QString             errorCaption;
  QString             fileName;
  int                 fromPage;
  int                 toPage;
};


QDjViewExporter::QDjViewExporter(QDialog *parent, QDjView *djview)
  : QObject(parent),
    parent(parent),
    djview(djview),
    errorDialog(0),
    fromPage(0),
    toPage(-1)
{
}


ddjvu_status_t 
QDjViewExporter::status()
{
  return DDJVU_JOB_NOTSTARTED;
}


void
QDjViewExporter::stop()
{
}


void 
QDjViewExporter::error(QString message, QString filename, int lineno)
{
  if (! errorDialog)
    {
      errorDialog = new QDjViewErrorDialog(parent);
      errorDialog->prepare(QMessageBox::Critical, errorCaption);
      connect(errorDialog, SIGNAL(closing()), parent, SLOT(reject()));
#if QT_VERSION >= 0x040100
      errorDialog->setWindowModality(Qt::WindowModal);
#else
      errorDialog->setModal(true);
#endif
    }
  errorDialog->error(message, filename, lineno);
  errorDialog->show();
}



// -------------------
// QDJVIEWDJVUEXPORTER



class QDjViewDjVuExporter : public QDjViewExporter
{
  Q_OBJECT
public:
  ~QDjViewDjVuExporter();
  QDjViewDjVuExporter(QDialog *parent, QDjView *djview, bool indirect);
  virtual void run();
  virtual void stop();
  virtual ddjvu_status_t status();
protected:
  QDjVuJob *job;
  FILE *output;
  bool indirect;
  bool failed;
};


QDjViewDjVuExporter::QDjViewDjVuExporter(QDialog *parent, QDjView *djview, 
                                         bool indirect)
  : QDjViewExporter(parent, djview),
    job(0), 
    output(0),
    indirect(indirect), 
    failed(false)
{
}


QDjViewDjVuExporter::~QDjViewDjVuExporter()
{
  if (output)
    ::fclose(output);
  output = 0;
  if (djview)
    ddjvu_cache_clear(djview->getDjVuContext());
  switch(status())
    {
    case DDJVU_JOB_STARTED:
      if (job)
        ddjvu_job_stop(*job);
    case DDJVU_JOB_FAILED:
    case DDJVU_JOB_STOPPED:
      if (! fileName.isEmpty())
        ::remove(QFile::encodeName(fileName).data());
    default:
      break;
    }
}
 

void 
QDjViewDjVuExporter::run()
{
  QDjVuDocument *document = djview->getDocument();
  int pagenum = djview->pageNum();
  if (document==0 || pagenum <= 0 || fileName.isEmpty())
    return;
#if QT_VERSION >= 0x40100
  QFileInfo info(fileName);
  QDir::Filters filters = QDir::AllEntries|QDir::NoDotAndDotDot;
  if (indirect && !info.dir().entryList(filters).isEmpty() &&
      QMessageBox::question(parent,
                            tr("Question - DjView", "dialog caption"),
                            tr("<html> This file belongs to a non empty "
                               "directory. Saving an indirect document "
                               "creates many files in this directory. "
                               "Do you want to continue and risk "
                               "overwriting files in this directory?"
                               "</html>"),
                            tr("Con&tinue"),
                            tr("&Cancel") ) )
    return;
#endif
  toPage = qBound(0, toPage, pagenum-1);
  fromPage = qBound(0, fromPage, pagenum-1);
  QByteArray pagespec;
  if (fromPage == toPage)
    pagespec = QString("--pages=%1")
      .arg(fromPage+1).toLocal8Bit();
  else if (qMin(fromPage, toPage)>0 || qMax(fromPage, toPage)<pagenum-1)
    pagespec = QString("--pages=%1-%2")
      .arg(fromPage+1).arg(toPage+1).toLocal8Bit();
  
  QByteArray namespec;
  if (indirect)
    namespec = "--indirect=" + QFile::encodeName(fileName);

  int argc = 0;
  const char *argv[2];
  if (! namespec.isEmpty())
    argv[argc++] = namespec.data();
  if (! pagespec.isEmpty())
    argv[argc++] = pagespec.data();
  
  QByteArray fname = QFile::encodeName(fileName);
  ::remove(fname.data());
  output = ::fopen(fname.data(), "w");
  if (! output)
    {
      failed = true;
      QString message = tr("System error: %1").arg(strerror(errno));
      error(message, __FILE__, __LINE__);
      return;
    }
  ddjvu_job_t *pjob;
  pjob = ddjvu_document_save(*document, output, argc, argv);
  if (! pjob)
    {
      failed = true;
      QString message = tr("Save job creation failed!");
      error(message, __FILE__, __LINE__);
      return;
    }
  job = new QDjVuJob(pjob, this);
  connect(job, SIGNAL(error(QString,QString,int)),
          this, SLOT(error(QString,QString,int)) );
  connect(job, SIGNAL(progress(int)),
          this, SIGNAL(progress(int)) );
  emit progress(0);
}


void 
QDjViewDjVuExporter::stop()
{
  if (job && status() == DDJVU_JOB_STARTED)
    ddjvu_job_stop(*job);
}


ddjvu_status_t 
QDjViewDjVuExporter::status()
{
  if (job)
    return ddjvu_job_status(*job);
  else if (failed)
    return DDJVU_JOB_FAILED;
  else
    return DDJVU_JOB_NOTSTARTED;
}




// -----------------
// QDJVIEWPSEXPORTER


#include "ui_qdjviewexportps1.h"
#include "ui_qdjviewexportps2.h"
#include "ui_qdjviewexportps3.h"


class QDjViewPSExporter : public QDjViewExporter
{
  Q_OBJECT
public:
  ~QDjViewPSExporter();
  QDjViewPSExporter(QDialog *parent, QDjView *djview, 
                    QString group, bool eps=false);
  virtual void run();
  virtual void stop();
  virtual ddjvu_status_t status();
  virtual void setPrinter(QPrinter *p);
  virtual QWidget* propertyPage(int);
  virtual bool canExportOnePage()    { return true; }
  virtual bool canExportPageRange()  { return !encapsulated; }
  virtual int propertyPages()        { return encapsulated ? 1 : 3; }
public slots:
  void refresh();
protected:
  QDjVuJob *job;
  FILE *output;
  bool failed;
  bool encapsulated;
  QString group;
  QString printerName;
  QPointer<QWidget> page1;
  QPointer<QWidget> page2;
  QPointer<QWidget> page3;
  Ui::QDjViewExportPS1 ui1;
  Ui::QDjViewExportPS2 ui2;
  Ui::QDjViewExportPS3 ui3;
};


QDjViewPSExporter::~QDjViewPSExporter()
{
  // save properties
  QSettings s(DJVIEW_ORG, DJVIEW_APP);
  s.beginGroup("Export-" + group);
  s.setValue("color", ui1.colorButton->isChecked());
  s.setValue("frame", ui1.frameCheckBox->isChecked());
  s.setValue("cropMarks", ui1.cropMarksCheckBox->isChecked());
  s.setValue("psLevel", ui1.levelSpinBox->value());
  s.setValue("autoOrient", ui2.autoOrientCheckBox->isChecked());
  s.setValue("landscape", ui2.landscapeButton->isChecked());
  int zoom = ui2.zoomSpinBox->value();
  s.setValue("zoom", QVariant(ui2.zoomButton->isChecked() ? zoom : 0));
  s.setValue("booklet", ui3.bookletCheckBox->isChecked());
  s.setValue("bookletSheets", ui3.sheetsSpinBox->value());
  s.setValue("bookletRectoVerso", ui3.rectoVersoCombo->currentIndex());
  s.setValue("bookletShift", ui3.rectoVersoShiftSpinBox->value());
  s.setValue("bookletCenter", ui3.centerMarginSpinBox->value());
  s.setValue("bookletCenterAdd", ui3.centerIncreaseSpinBox->value());
  // delete property pages
  delete page1;
  delete page2;
  delete page3;
  // cleanup output
  if (output)
    ::fclose(output);
  output = 0;
  switch(status())
    {
    case DDJVU_JOB_STARTED:
      if (job)
        ddjvu_job_stop(*job);
    case DDJVU_JOB_FAILED:
    case DDJVU_JOB_STOPPED:
      if (! fileName.isEmpty())
        ::remove(QFile::encodeName(fileName).data());
    default:
      break;
    }
}


QDjViewPSExporter::QDjViewPSExporter(QDialog *parent, QDjView *djview, 
                                     QString group, bool eps)
  : QDjViewExporter(parent, djview),
    job(0), 
    output(0),
    failed(false),
    encapsulated(eps),
    group(group)
{
  // create pages
  page1 = new QWidget();
  page2 = new QWidget();
  page3 = new QWidget();
  ui1.setupUi(page1);
  ui2.setupUi(page2);
  ui3.setupUi(page3);
  page1->setObjectName(tr("PostScript","tab caption"));
  page2->setObjectName(tr("Position","tab caption"));
  page3->setObjectName(tr("Booklet","tab caption"));
  // load settings (ui1)
  QSettings s(DJVIEW_ORG, DJVIEW_APP);
  s.beginGroup("Export-" + group);
  bool color = s.value("color", true).toBool();
  ui1.colorButton->setChecked(color);
  ui1.grayScaleButton->setChecked(!color);
  ui1.frameCheckBox->setChecked(s.value("frame", false).toBool());
  ui1.cropMarksCheckBox->setChecked(s.value("cropMarks", false).toBool());
  int level = s.value("psLevel", 3).toInt();
  ui1.levelSpinBox->setValue(qBound(1,level,3));
  // load settings (ui2)  
  ui2.autoOrientCheckBox->setChecked(s.value("autoOrient", true).toBool());
  bool landscape = s.value("landscape",false).toBool();
  ui2.portraitButton->setChecked(!landscape);
  ui2.landscapeButton->setChecked(landscape);
  int zoom = s.value("zoom",0).toInt();
  ui2.scaleToFitButton->setChecked(zoom == 0);
  ui2.zoomButton->setChecked(zoom!=0);
  ui2.zoomSpinBox->setValue(zoom ? qBound(10,zoom,1000) : 100);
  // load settings (ui3)  
  ui3.bookletCheckBox->setChecked(s.value("booklet", false).toBool());
  ui3.sheetsSpinBox->setValue(s.value("bookletSheets", 0).toInt());
  int rectoVerso = qBound(0, s.value("bookletRectoVerso",0).toInt(), 2);
  ui3.rectoVersoCombo->setCurrentIndex(rectoVerso);
  int rectoVersoShift = qBound(-72, s.value("bookletShift",0).toInt(), 72);
  ui3.rectoVersoShiftSpinBox->setValue(rectoVersoShift);
  int centerMargin = qBound(0, s.value("bookletCenter", 18).toInt(), 144);
  ui3.centerMarginSpinBox->setValue(centerMargin);
  int centerIncrease = qBound(0, s.value("bookletCenterAdd", 40).toInt(), 200);
  ui3.centerIncreaseSpinBox->setValue(centerIncrease);
  // connect stuff
  connect(ui2.autoOrientCheckBox, SIGNAL(clicked()), 
          this, SLOT(refresh()) );
  connect(ui3.bookletCheckBox, SIGNAL(clicked()), 
          this, SLOT(refresh()) );
  connect(ui2.zoomSpinBox, SIGNAL(valueChanged(int)), 
          ui2.zoomButton, SLOT(click()) );
  // adjust ui
  refresh();
}


void 
QDjViewPSExporter::setPrinter(QPrinter *p)
{
  printerName = p->printerName();
  // MORE TODO
}


void 
QDjViewPSExporter::refresh()
{
  if (encapsulated)
    {
      ui1.cropMarksCheckBox->setEnabled(false);
      ui1.cropMarksCheckBox->setChecked(false);
    }
  bool autoOrient = ui2.autoOrientCheckBox->isChecked();
  ui2.portraitButton->setEnabled(!autoOrient);
  ui2.landscapeButton->setEnabled(!autoOrient);
  bool booklet = ui3.bookletCheckBox->isChecked();
  ui3.bookletGroupBox->setEnabled(booklet);
}


void 
QDjViewPSExporter::run()
{
}


void 
QDjViewPSExporter::stop()
{
  if (job && status() == DDJVU_JOB_STARTED)
    ddjvu_job_stop(*job);
}


ddjvu_status_t 
QDjViewPSExporter::status()
{
  if (job)
    return ddjvu_job_status(*job);
  else if (failed)
    return DDJVU_JOB_FAILED;
  else
    return DDJVU_JOB_NOTSTARTED;
}


QWidget* 
QDjViewPSExporter::propertyPage(int n)
{
  if (n == 0)
    return page1;
  else if (n == 1)
    return page2;
  else if (n == 2)
    return page3;
  return 0;
}



// --------------------
// QDJVIEWPRNEXPORTER



// -------------------
// QDJVIEWTIFFEXPORTER



// -------------------
// QDJVIEWIMGEXPORTER





// =======================================
// QDJVIEWSAVEDIALOG
// =======================================


#include "ui_qdjviewsavedialog.h"


struct QDjViewSaveDialog::Private
{
  QDjView *djview;
  QDjVuDocument *document;
  Ui::QDjViewSaveDialog ui;
  bool stopping;

  int exporterIndex;
  QList<QDjViewExporter*> exporters;
  QStringList exporterFilters;
  QStringList exporterExtensions;
};




QDjViewSaveDialog::QDjViewSaveDialog(QDjView *djview)
  : QDialog(djview), d(0)
{
  d = new QDjViewSaveDialog::Private;

  d->djview = djview;
  d->document = 0;
  d->stopping = false;
  d->exporterIndex = 0;

  d->ui.setupUi(this);
  setAttribute(Qt::WA_GroupLeader, true);

  connect(d->ui.okButton, SIGNAL(clicked()), 
          this, SLOT(start()));
  connect(d->ui.cancelButton, SIGNAL(clicked()), 
          this, SLOT(reject()));
  connect(d->ui.stopButton, SIGNAL(clicked()), 
          this, SLOT(stop()));
  connect(d->ui.browseButton, SIGNAL(clicked()), 
          this, SLOT(browse()));
  connect(d->ui.formatCombo, SIGNAL(activated(int)), 
          this, SLOT(refresh()));
  connect(d->ui.fileNameEdit, SIGNAL(textChanged(QString)), 
          this, SLOT(refresh()));
  connect(d->ui.fromPageCombo, SIGNAL(activated(int)),
          d->ui.pageRangeButton, SLOT(click()) );
  connect(d->ui.toPageCombo, SIGNAL(activated(int)),
          d->ui.pageRangeButton, SLOT(click()) );
  connect(djview, SIGNAL(documentClosed(QDjVuDocument*)),
          this, SLOT(clear()));
  connect(djview, SIGNAL(documentReady(QDjVuDocument*)),
          this, SLOT(refresh()));

  setWhatsThis(tr("<html><b>Saving DjVu data.</b><br/> "
                  "You can save the whole document or a page range "
                  "under a variety of formats. The format \"Options\" "
                  "button is enabled when additional parameters can "
                  "be set for this format.</html>"));

  addExporter(tr("DjVu Bundled Document"),
              tr("DjVu File (*.djvu *.djv)"), ".djvu",
              new QDjViewDjVuExporter(this, d->djview, false));
  addExporter(tr("DjVu Indirect Document"),
              tr("DjVu File (*.djvu *.djv)"), ".djvu",
              new QDjViewDjVuExporter(this, d->djview, true));
  addExporter(tr("PostScript"),
              tr("PostScript File (*.ps)"), ".ps",
              new QDjViewPSExporter(this, d->djview, "PS", false));
  addExporter(tr("Encapsulated PostScript"),
              tr("Encapsulated PostScript File (*.eps)"), ".eps",
              new QDjViewPSExporter(this, d->djview, "EPS", true));
  
  refresh();
}



void 
QDjViewSaveDialog::refresh()
{
  bool nodoc = !d->document;
  if (nodoc && d->djview->pageNum() > 0)
    {
      nodoc = false;
      d->document = d->djview->getDocument();
      QString fname = d->djview->getDocumentFileName();
      if (fname.isEmpty())
        fname = d->djview->getShortFileName();
      else 
        fname = QFileInfo(fname).absoluteFilePath();
      d->ui.fileNameEdit->setText(fname);
      d->djview->fillPageCombo(d->ui.fromPageCombo);
      d->ui.fromPageCombo->setCurrentIndex(0);
      d->djview->fillPageCombo(d->ui.toPageCombo);
      d->ui.toPageCombo->setCurrentIndex(d->djview->pageNum()-1);
    }
  bool notxt = d->ui.fileNameEdit->text().isEmpty();
  bool nojob = true;
  bool nofmt = true;
  bool noopt = true;
  bool nopag = true;
  int oldIndex = d->exporterIndex;
  d->exporterIndex = d->ui.formatCombo->currentIndex();
  QDjViewExporter *exporter = currentExporter();
  if (exporter && d->exporterIndex != oldIndex)
    {
      // update tabs
      while (d->ui.tabWidget->count() > 1)
        d->ui.tabWidget->removeTab(1);
      QWidget *w;
      for (int i=0; i<exporter->propertyPages(); i++)
        if ((w = exporter->propertyPage(i)))
          d->ui.tabWidget->addTab(w, w->objectName());
      // update filename
      QString fname = d->ui.fileNameEdit->text();
      QString ext = d->exporterExtensions[d->exporterIndex]; 
      QFileInfo finfo(fname);
      if (!finfo.completeSuffix().isEmpty() && !ext.isEmpty())
        fname = QFileInfo(finfo.dir(), finfo.baseName() + ext).filePath();
      d->ui.fileNameEdit->setText(fname);
    }
  if (exporter)
    {
      nofmt = false;
      if (exporter->status()>=DDJVU_JOB_STARTED)
        nojob = false;
      if (exporter->propertyPages() > 0)
        noopt = false;
      disconnect(d->ui.fromPageCombo, 0, d->ui.toPageCombo, 0);
      d->ui.documentButton->setEnabled(true);
      d->ui.toPageCombo->setEnabled(true);
      d->ui.toLabel->setEnabled(true);
      if (exporter->canExportPageRange() || exporter->canExportOnePage())
        {
          nopag = false;
          if (! exporter->canExportPageRange())
            {
              connect(d->ui.fromPageCombo, SIGNAL(activated(int)),
                      d->ui.toPageCombo, SLOT(setCurrentIndex(int)));
              d->ui.toPageCombo->setEnabled(false);
              d->ui.toLabel->setEnabled(false);
              d->ui.documentButton->setEnabled(false);
              if (d->ui.documentButton->isChecked())
                d->ui.currentPageButton->setChecked(true);
              if (! nodoc)
                {
                  int page = d->ui.fromPageCombo->currentIndex();
                  d->ui.toPageCombo->setCurrentIndex(page);
                }
            }
        }
    }
  d->ui.destinationGroupBox->setEnabled(nojob && !nodoc);
  d->ui.saveGroupBox->setEnabled(nojob && !nodoc && !nofmt && !nopag);
  d->ui.okButton->setEnabled(nojob && !nodoc && !notxt && !nofmt);
  d->ui.cancelButton->setEnabled(nojob);
  d->ui.stopButton->setEnabled(!nojob);
  d->ui.stackedWidget->setCurrentIndex(nojob ? 0 : 1);
}


void 
QDjViewSaveDialog::clear()
{
  d->stopping = true;
  reject();
}


void 
QDjViewSaveDialog::start()
{
  QString fname = d->ui.fileNameEdit->text();
  QFileInfo info(fname);
  if (info.exists())
    {
      QString docname = d->djview->getDocumentFileName();
      if (info == QFileInfo(docname) && ! docname.isEmpty())
        {
          QMessageBox::critical(this, 
                                tr("Error - DjView", "dialog caption"),
                                tr("Overwriting the current file "
                                   "is not allowed!" ) );
          return;
        }
      if ( QMessageBox::question(this, 
                                 tr("Question - DjView", "dialog caption"),
                                 tr("A file with this name already exists.\n"
                                    "Do you want to replace it?"),
                                 tr("&Replace"),
                                 tr("&Cancel") ))
        return;
    }
  
  QDjViewExporter *exporter = currentExporter();
  if (exporter)
    {
      int lastPage = d->djview->pageNum()-1;
      int curPage = d->djview->getDjVuWidget()->page();
      int fromPage = d->ui.fromPageCombo->currentIndex();
      int toPage = d->ui.toPageCombo->currentIndex();
      exporter->setFileName(fname);
      if (d->ui.currentPageButton->isChecked())
        exporter->setPageRange(curPage, curPage);
      else if (d->ui.pageRangeButton->isChecked())
        exporter->setPageRange(fromPage, toPage);
      else
        exporter->setPageRange(0, lastPage);
      // ignition!
      exporter->run();
    }

  refresh();
}


void 
QDjViewSaveDialog::progress(int percent)
{
  QDjViewExporter *exporter = currentExporter();
  ddjvu_status_t jobstatus = DDJVU_JOB_NOTSTARTED;
  if (exporter)
    jobstatus = exporter->status();
  d->ui.progressBar->setValue(percent);
  switch(jobstatus)
    {
    case DDJVU_JOB_OK:
      accept();
      break;
    case DDJVU_JOB_FAILED:
      exporter->error(tr("This operation has failed."),
                      __FILE__, __LINE__);
      break;
    case DDJVU_JOB_STOPPED:
      exporter->error(tr("This operation has been interrupted."),
                      __FILE__, __LINE__);
      break;
    default:
      break;
    }
}


void 
QDjViewSaveDialog::stop()
{
  QDjViewExporter *exporter = currentExporter();
  if (exporter && exporter->status() == DDJVU_JOB_STARTED)
    {
      exporter->stop();
      d->ui.stopButton->setEnabled(false);
      d->stopping = true;
    }
}


void 
QDjViewSaveDialog::browse()
{
  QString fname = d->ui.fileNameEdit->text();
  QDjViewExporter *exporter = currentExporter();
  QString filters;
  if (exporter)
    filters += d->exporterFilters[d->exporterIndex] + ";;";
  filters += tr("All files", "save filter") + " (*)";
  fname = QFileDialog::getSaveFileName(this, 
                                       tr("Save - DjView", "dialog caption"),
                                       fname, filters, 0,
                                       QFileDialog::DontConfirmOverwrite);
  QFileInfo finfo(fname);
  QString ext = d->exporterExtensions[d->exporterIndex]; 
  if (finfo.completeSuffix().isEmpty() && !ext.isEmpty())
    fname = QFileInfo(finfo.dir(), finfo.baseName() + ext).filePath();
  d->ui.fileNameEdit->setText(fname);
}


void 
QDjViewSaveDialog::done(int reason)
{
  setEnabled(false);
  QDjViewExporter *exporter = currentExporter();
  if (!d->stopping && exporter && exporter->status()==DDJVU_JOB_STARTED)
    {
      stop();
      return;
    }
  foreach(exporter, d->exporters)
    delete exporter;
  d->exporters.clear();
  QDialog::done(reason);
}


void 
QDjViewSaveDialog::closeEvent(QCloseEvent *event)
{
  QDjViewExporter *exporter = currentExporter();
  if (!d->stopping && exporter && exporter->status()==DDJVU_JOB_STARTED)
    {
      stop();
      event->ignore();
      return;
    }
  foreach(exporter, d->exporters)
    delete exporter;
  d->exporters.clear();
  event->accept();
  QDialog::closeEvent(event);
}


QDjViewExporter *
QDjViewSaveDialog::currentExporter()
{
  if (d->exporterIndex >= 0)
    if (d->exporterIndex < d->exporters.count())
      return d->exporters.at(d->exporterIndex);
  return 0;
}


int 
QDjViewSaveDialog::addExporter(QString name, QString filter, QString extension,
                               QDjViewExporter *exporter)
{
  int index = d->exporters.count();
  d->ui.formatCombo->addItem(name, QVariant(index));
  d->ui.formatCombo->setCurrentIndex(0);
  d->exporterExtensions += extension;
  d->exporterFilters += filter;
  d->exporters += exporter;
  connect(exporter, SIGNAL(progress(int)), this, SLOT(progress(int)));
  exporter->setObjectName(name);
  exporter->setErrorCaption(tr("Save Error - DjView", "dialog caption"));
  return index;
}






// =======================================
// QDJVIEWPRINTDIALOG
// =======================================


#include "ui_qdjviewprintdialog.h"

struct QDjViewPrintDialog::Private {
  Ui::QDjViewPrintDialog ui;
  QDjView *djview;
  QDjVuDocument *document;
  QDjVuJob *job;
  QDjVuPage *page;
  QPrinter *printer;
  QPrintDialog *printdialog;
  QDjViewErrorDialog *errdialog;
  FILE *output;
  bool stop;
};


QDjViewPrintDialog::~QDjViewPrintDialog()
{
  delete d->printer;
  delete d;
}

QDjViewPrintDialog::QDjViewPrintDialog(QDjView *djview)
  : d(new Private)
{
  d->djview = djview;
  d->document = 0;
  d->job = 0;
  d->page = 0;
  d->printer = 0;
  d->printdialog = 0;
  d->errdialog = 0;
  d->output = 0;
  d->stop = false;
  d->ui.setupUi(this);
  
  d->printer = new QPrinter();
  d->printdialog = new QPrintDialog(d->printer, this);
  d->printdialog->addEnabledOption(QPrintDialog::PrintToFile);
  d->printdialog->addEnabledOption(QPrintDialog::PrintSelection);
  d->printdialog->addEnabledOption(QPrintDialog::PrintCollateCopies);
  
  connect(d->djview, SIGNAL(documentClosed(QDjVuDocument*)),
          this, SLOT(reject()) );
  connect(d->djview, SIGNAL(documentReady(QDjVuDocument*)),
          this, SLOT(refresh()) );

  connect(d->ui.psButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.epsButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.nativeButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.pdfButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.bookletCheckBox, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.documentButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.currentPageButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.pageRangeButton, SIGNAL(clicked()), this, SLOT(refresh()));
  connect(d->ui.fromPageCombo, SIGNAL(activated(int)), this, SLOT(refresh()));
  connect(d->ui.toPageCombo, SIGNAL(activated(int)), this, SLOT(refresh()));
  connect(d->ui.fromPageCombo, SIGNAL(activated(int)),
          d->ui.pageRangeButton, SLOT(click()) );
  connect(d->ui.toPageCombo, SIGNAL(activated(int)),
          d->ui.pageRangeButton, SLOT(click()) );
  connect(d->ui.zoomSpinBox, SIGNAL(valueChanged(int)),
          d->ui.zoomButton, SLOT(click()) );
  connect(d->ui.changePrinterButton, SIGNAL(clicked()),
          this, SLOT(choosePrinter()) );
  connect(d->ui.cancelButton, SIGNAL(clicked() ),
          this, SLOT(reject()) );
  connect(d->ui.printButton, SIGNAL(clicked() ),
          this, SLOT(print()) );

  refresh();
}



bool 
QDjViewPrintDialog::isPrinterPostScript()
{
  if (! d->printer->outputFileName().isEmpty())
    return true;
#ifdef Q_WS_WIN
  // TODO:
  // Use EnumPrinter to get a PRINTER_INFO_2 structure.
  // Check that the driver is the postscript driver.
  return false;
#else
  return true;
#endif
}


void 
QDjViewPrintDialog::updatePrinter()
{
  int npages = d->djview->pageNum();
  d->printer->setCollateCopies(d->ui.collateCheckBox->isChecked());
  d->printer->setCreator("djview");
  d->printer->setDocName(d->djview->getShortFileName());
  d->printer->setNumCopies(d->ui.numCopiesSpinBox->value());
  if (d->ui.landscapeButton->isChecked())
    d->printer->setOrientation(QPrinter::Landscape);
  else
    d->printer->setOrientation(QPrinter::Portrait);
  if (d->ui.colorButton->isChecked())
    d->printer->setColorMode(QPrinter::Color);
  else
    d->printer->setColorMode(QPrinter::GrayScale);
  int fPage = d->ui.fromPageCombo->currentIndex();
  int lPage = d->ui.toPageCombo->currentIndex();
  if (d->ui.currentPageButton->isChecked())
    { fPage = lPage = d->djview->getDjVuWidget()->page(); }
  if (d->ui.documentButton->isChecked())
    { fPage = 0; lPage = npages - 1; }
  if (fPage>lPage && d->ui.pageRangeButton->isChecked())
    d->printer->setPageOrder(QPrinter::LastPageFirst);
  else
    d->printer->setPageOrder(QPrinter::FirstPageFirst);
  d->printdialog->setMinMax(1, npages);
  d->printdialog->setFromTo(qMin(fPage,lPage)+1, qMax(fPage,lPage)+1);
  if (d->ui.pageRangeButton->isChecked() 
      && (qMin(fPage,lPage)!=0 || qMax(fPage,lPage)!=npages-1) ) 
    d->printdialog->setPrintRange(QPrintDialog::PageRange);
  else if (d->ui.currentPageButton->isChecked())
    d->printdialog->setPrintRange(QPrintDialog::Selection);
  else
    d->printdialog->setPrintRange(QPrintDialog::AllPages);
#if QT_VERSION >= 0x40100
  if (d->ui.pdfButton->isChecked())
    d->printer->setOutputFormat(QPrinter::PdfFormat);
  else
    d->printer->setOutputFormat(QPrinter::NativeFormat);
#endif
}


void 
QDjViewPrintDialog::choosePrinter()
{
  // standard print dialog
  updatePrinter();
  d->printdialog->exec();
  this->raise();
  // update ui
  int npages = d->djview->pageNum();
  int fPage = d->ui.fromPageCombo->currentIndex();
  int lPage = d->ui.toPageCombo->currentIndex();
  QPrinter::PageOrder order = d->printer->pageOrder();
  QPrintDialog::PrintRange range = d->printdialog->printRange();
  d->ui.documentButton->setChecked(range == QPrintDialog::AllPages);
  d->ui.currentPageButton->setChecked(range == QPrintDialog::Selection);
  d->ui.pageRangeButton->setChecked(range == QPrintDialog::PageRange);
  if (range == QPrintDialog::PageRange)
    {
      fPage = d->printdialog->fromPage()-1;
      lPage = d->printdialog->toPage()-1;
      fPage = qBound(0, fPage, npages-1);
      lPage = qBound(0, lPage, npages-1);
      if ((order == QPrinter::FirstPageFirst) && (fPage > lPage))
        qSwap(fPage, lPage);
      if ((order == QPrinter::LastPageFirst) && (fPage < lPage))
        qSwap(fPage, lPage);
      d->ui.fromPageCombo->setCurrentIndex(fPage);
      d->ui.toPageCombo->setCurrentIndex(lPage);
    }
  if (d->ui.documentButton->isChecked())
    if (order == QPrinter::LastPageFirst)
      {
        d->ui.pageRangeButton->setChecked(true);
        d->ui.fromPageCombo->setCurrentIndex(npages-1);
        d->ui.toPageCombo->setCurrentIndex(0);
      }
  d->ui.collateCheckBox->setChecked(d->printer->collateCopies());
  d->ui.numCopiesSpinBox->setValue(d->printer->numCopies());
  if (! d->ui.autoOrientButton->isChecked())
    {
      QPrinter::Orientation orient = d->printer->orientation();
      d->ui.portraitButton->setChecked(orient == QPrinter::Portrait);
      d->ui.landscapeButton->setChecked(orient == QPrinter::Landscape);
    }
  QPrinter::ColorMode color = d->printer->colorMode();
  d->ui.colorButton->setChecked(color == QPrinter::Color);
  d->ui.grayScaleButton->setChecked(color == QPrinter::GrayScale);
  QString printerName = d->printer->printerName();
  QString fileName = d->printer->outputFileName();
  QString msg;
  if (! fileName.isEmpty())
    msg = tr("File: %1").arg(fileName);
  else if (! printerName.isEmpty())
    msg = tr("Printer: %1").arg(printerName);
  else
    msg = tr("No destination");
  QFontMetrics metric(d->ui.destinationLabel->font());
  int width = qMax(358, d->ui.destinationLabel->width()) - 8;
  msg = QItemDelegate::elidedText(metric, width, Qt::ElideMiddle, msg);
  d->ui.destinationLabel->setText(msg);
  // finalize
  refresh();
}


void 
QDjViewPrintDialog::refresh()
{
  int npages = d->djview->pageNum();
  if (!d->document && npages>0)
    {
      d->document = d->djview->getDocument();
      d->djview->fillPageCombo(d->ui.fromPageCombo);
      d->ui.fromPageCombo->setCurrentIndex(0);
      d->djview->fillPageCombo(d->ui.toPageCombo);
      d->ui.toPageCombo->setCurrentIndex(npages-1);
    }

  if (! d->document)
    {
      d->ui.stackedWidget->setCurrentIndex(0);
      d->ui.tabWidget->setEnabled(false);
      d->ui.printButton->setEnabled(false);
      d->ui.cancelButton->setEnabled(true);
      d->ui.stopButton->setEnabled(false);
      return;
    }
  else if (d->job || d->page)
    {
      d->ui.stackedWidget->setCurrentIndex(1);
      d->ui.tabWidget->setEnabled(false);
      d->ui.printButton->setEnabled(false);
      d->ui.cancelButton->setEnabled(false);
      d->ui.stopButton->setEnabled(true);
      return;
    }
  else
    {
      bool okfile = !d->printer->outputFileName().isEmpty();
      bool okprinter = !d->printer->printerName().isEmpty();
      bool okps = isPrinterPostScript();

      d->ui.stackedWidget->setCurrentIndex(0);
      d->ui.tabWidget->setEnabled(true);
      d->ui.printButton->setEnabled(okfile||okprinter);
      d->ui.cancelButton->setEnabled(true);
      d->ui.stopButton->setEnabled(false);
      
      d->ui.collateCheckBox->setEnabled(okprinter && !okfile);
      d->ui.numCopiesSpinBox->setEnabled(okprinter && !okfile);
      if (okfile)
        d->ui.numCopiesSpinBox->setValue(1);
      
      bool onepage = (npages == 1);
      if (d->ui.currentPageButton->isChecked())
        onepage = true;
      if (d->ui.pageRangeButton->isChecked() &&
          d->ui.fromPageCombo->currentIndex() 
          == d->ui.toPageCombo->currentIndex())
        onepage = true;
      d->ui.psButton->setEnabled(okps);
      if (! (okps))
        d->ui.psButton->setChecked(false);
      d->ui.epsButton->setEnabled(okps && okfile && onepage);
      if (! (okps && okfile && onepage))
        d->ui.epsButton->setChecked(false);
#if QT_VERSION >= 0x40100
      d->ui.pdfButton->setEnabled(okfile);
      if (! (okfile))
        d->ui.pdfButton->setChecked(false);
#else
      d->ui.pdfButton->setEnabled(false);
#endif
      if (! (okfile))
        d->ui.pdfButton->setChecked(false);
      bool psmode = false;
      if (d->ui.psButton->isChecked() ||
          d->ui.epsButton->isChecked() )
        psmode = true;
      d->ui.levelGroupBox->setEnabled(okps && psmode);
      d->ui.marksGroupBox->setEnabled(okps && psmode);
      d->ui.autoOrientButton->setEnabled(okps && psmode);
      d->ui.bookletWidget->setEnabled(okps && psmode);
      d->ui.bookletCheckBox->setEnabled(okps);
      if (! okps)
        d->ui.bookletCheckBox->setChecked(false);
      bool booklet = d->ui.bookletCheckBox->isChecked();
      QObject *o;
      QWidget *w;
      foreach(o, d->ui.bookletGroupBox->children())
        if ((w = qobject_cast<QWidget*>(o)) && (w != d->ui.bookletCheckBox))
          w->setEnabled(booklet && okps);
      d->ui.advancedBookletGroupBox->setEnabled(booklet && okps);
    }
}


void 
QDjViewPrintDialog::progress(int percent)
{
  d->ui.progressBar->setValue(percent);
  if (d->job)
    {
      ddjvu_status_t status = ddjvu_job_status(*d->job);
      switch(status)
        {
        case DDJVU_JOB_OK:
          accept();
          break;
        case DDJVU_JOB_FAILED:
          error(tr("This job has failed."), __FILE__, __LINE__);
          break;
        case DDJVU_JOB_STOPPED:
          error(tr("This job has been interrupted."), __FILE__, __LINE__);
          break;
        default:
          break;
        }
    }
  if (d->stop)
    reject();
}


void 
QDjViewPrintDialog::print()
{
  // should not happen
  if (d->job || d->page)
    return;
  // start printing in whatever format
  updatePrinter();
  if (d->ui.psButton->isChecked() ||
      d->ui.epsButton->isChecked() )
    {
      printDjVu();
    }
  else
    {
      QTimer::singleShot(0, this, SLOT(printNative()));
    }
}


void 
QDjViewPrintDialog::printDjVu()
{
  // print one more page in native format
  error(tr("djvu output not yet implemented"),
        __FILE__, __LINE__);
}


void 
QDjViewPrintDialog::printNative()
{
  if (d->stop)
    reject();
  // print one more page in native format
  error(tr("native/pdf output not yet implemented"),
        __FILE__, __LINE__);
}


void 
QDjViewPrintDialog::stop()
{
  if (d->job)
    ddjvu_job_stop(*d->job);
  if (d->page)
    d->printer->abort();
  d->stop = true;
  d->ui.stopButton->setEnabled(false);
}


void 
QDjViewPrintDialog::error(QString message, QString filename, int lineno)
{
  if (! d->errdialog)
    {
      d->errdialog = new QDjViewErrorDialog(this);
      QString caption = tr("Print Error - DjView", "dialog caption");
      d->errdialog->prepare(QMessageBox::Critical, caption);
      connect(d->errdialog, SIGNAL(closing()), this, SLOT(reject()));
#if QT_VERSION >= 0x040100
      d->errdialog->setWindowModality(Qt::WindowModal);
#else
      d->errdialog->setModal(true);
#endif
    }
  d->errdialog->error(message, filename, lineno);
  d->errdialog->show();
}


void 
QDjViewPrintDialog::closeEvent(QCloseEvent *event)
{
  if (d->job || d->page)
    {
      stop();
      event->ignore();
      return;
    }
  QDialog::closeEvent(event);
}


void 
QDjViewPrintDialog::done(int result)
{
  if (d->job || d->page) 
    stop();
  if (d->output && result == QDialog::Rejected)
    { /* remove printed file */ }
  if (d->output)
    fclose(d->output);
  d->output = 0;
  QDialog::done(result);
}






// ----------------------------------------
// MOC

#include "qdjviewdialogs.moc"




/* -------------------------------------------------------------
   Local Variables:
   c++-font-lock-extra-types: ( "\\sw+_t" "[A-Z]\\sw*[a-z]\\sw*" )
   End:
   ------------------------------------------------------------- */


