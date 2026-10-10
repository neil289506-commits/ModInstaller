#include "ModBrowserDialog.h"
#include "DependencyReviewDialog.h"
#include "IconCache.h"
#include "ProgressDialog.h"
#include "core/DependencyResolver.h"
#include "core/ModApi.h"
#include "core/Util.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

namespace MI {

namespace {
enum Roles { NameRole = Qt::UserRole + 1, AuthorRole, SummaryRole, IconRole, DownloadsRole, StateRole };
}

class ResultDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(100, 84); }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(opt.rect).adjusted(4, 3, -4, -3);
        const bool sel = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        p->setPen(Qt::NoPen);
        p->setBrush(sel ? QColor(0x2c, 0x3a, 0x33) : (hover ? QColor(0x2b, 0x2f, 0x36) : QColor(0x23, 0x26, 0x2c)));
        p->drawRoundedRect(r, 8, 8);
        if (sel) {
            p->setPen(QPen(QColor(0x1b, 0xd9, 0x6a), 1.5));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(r, 8, 8);
        }
        const QPixmap icon = IconCache::i().get(idx.data(IconRole).toString());
        p->drawPixmap(QRectF(r.left() + 10, r.top() + 10, 56, 56), icon, QRectF(icon.rect()));

        const qreal tx = r.left() + 78;
        const qreal tw = r.width() - 90;
        QFont f = opt.font;
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() + 1);
        p->setFont(f);
        p->setPen(QColor(0xec, 0xee, 0xf1));
        const QString name = idx.data(NameRole).toString();
        QString state = idx.data(StateRole).toString();
        p->drawText(QRectF(tx, r.top() + 8, tw - (state.isEmpty() ? 0 : 90), 22), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetricsF(f).elidedText(name, Qt::ElideRight, tw - (state.isEmpty() ? 0 : 90)));
        if (!state.isEmpty()) {
            p->setPen(QColor(0x1b, 0xd9, 0x6a));
            QFont sf = opt.font;
            sf.setPointSizeF(sf.pointSizeF() - 1);
            p->setFont(sf);
            p->drawText(QRectF(r.right() - 100, r.top() + 8, 90, 22), Qt::AlignRight | Qt::AlignVCenter, state);
        }
        QFont nf = opt.font;
        nf.setPointSizeF(nf.pointSizeF() - 1);
        p->setFont(nf);
        p->setPen(QColor(0x8a, 0x94, 0xa3));
        p->drawText(QRectF(tx, r.top() + 30, tw, 18), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("%1  ·  ⬇ %2").arg(idx.data(AuthorRole).toString(), humanCount(idx.data(DownloadsRole).toLongLong())));
        p->setPen(QColor(0xb0, 0xba, 0xc5));
        p->drawText(QRectF(tx, r.top() + 48, tw, 18), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetricsF(nf).elidedText(idx.data(SummaryRole).toString().simplified(), Qt::ElideRight, tw));
        p->restore();
    }
};

ModBrowserDialog::ModBrowserDialog(const PackTarget &target, const QList<InstalledMod> &installed, QWidget *parent)
    : QDialog(parent), m_target(target), m_installed(installed)
{
    setWindowTitle(tr("下載模組 — %1 %2 %3").arg(loaderName(target.loader), target.loaderVersion, target.mcVersion));
    setModal(true);
    resize(1180, 760);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    // 頂列：平台分頁 + 搜尋 + 排序
    m_tabs = new QTabBar(this);
    m_tabs->addTab(QStringLiteral("Modrinth"));
    m_tabs->addTab(QStringLiteral("CurseForge"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    root->addWidget(m_tabs);

    auto *top = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜尋模組…"));
    m_search->setClearButtonEnabled(true);
    m_sort = new QComboBox(this);
    m_sort->setMinimumWidth(170);
    auto *go = new QPushButton(tr("搜尋"), this);
    top->addWidget(m_search, 1);
    top->addWidget(m_sort);
    top->addWidget(go);
    root->addLayout(top);

    // 中間：左結果 / 右詳細
    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);

    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(left);
    m_list->setObjectName(QStringLiteral("resultList"));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setMouseTracking(true);
    m_delegate = new ResultDelegate(m_list);
    m_list->setItemDelegate(m_delegate);
    m_status = new QLabel(left);
    m_status->setObjectName(QStringLiteral("muted"));
    ll->addWidget(m_list, 1);
    ll->addWidget(m_status);

    auto *right = new QWidget(split);
    right->setObjectName(QStringLiteral("detailPanel"));
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(18, 18, 18, 18);
    rl->setSpacing(8);
    auto *hr = new QHBoxLayout;
    m_icon = new QLabel(right);
    m_icon->setFixedSize(72, 72);
    m_icon->setScaledContents(true);
    auto *hv = new QVBoxLayout;
    m_name = new QLabel(right);
    m_name->setObjectName(QStringLiteral("heading"));
    m_name->setWordWrap(true);
    m_meta = new QLabel(right);
    m_meta->setObjectName(QStringLiteral("muted"));
    hv->addWidget(m_name);
    hv->addWidget(m_meta);
    hr->addWidget(m_icon);
    hr->addLayout(hv, 1);
    rl->addLayout(hr);
    m_summary = new QLabel(right);
    m_summary->setWordWrap(true);
    m_summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rl->addWidget(m_summary, 1);
    m_link = new QLabel(right);
    m_link->setOpenExternalLinks(true);
    rl->addWidget(m_link);
    auto *vl = new QLabel(tr("版本"), right);
    vl->setObjectName(QStringLiteral("fieldLabel"));
    rl->addWidget(vl);
    m_versions = new QComboBox(right);
    rl->addWidget(m_versions);
    m_versionInfo = new QLabel(right);
    m_versionInfo->setObjectName(QStringLiteral("muted"));
    m_versionInfo->setWordWrap(true);
    rl->addWidget(m_versionInfo);
    m_selectBtn = new QPushButton(tr("選取"), right);
    rl->addWidget(m_selectBtn);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({700, 440});
    root->addWidget(split, 1);

    // 底部：已選取
    auto *sl = new QLabel(tr("已選取的模組"), this);
    sl->setObjectName(QStringLiteral("fieldLabel"));
    root->addWidget(sl);
    m_selList = new QListWidget(this);
    m_selList->setObjectName(QStringLiteral("selectedList"));
    m_selList->setFixedHeight(96);
    m_selList->setIconSize(QSize(28, 28));
    m_selList->setFlow(QListView::LeftToRight);
    m_selList->setWrapping(true);
    m_selList->setResizeMode(QListView::Adjust);
    m_selList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(m_selList);

    auto *bot = new QHBoxLayout;
    m_removeBtn = new QPushButton(tr("移除選取項目"), this);
    m_removeBtn->setObjectName(QStringLiteral("secondary"));
    auto *cancel = new QPushButton(tr("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    m_confirmBtn = new QPushButton(tr("確認選取"), this);
    m_confirmBtn->setDefault(true);
    bot->addWidget(m_removeBtn);
    bot->addStretch(1);
    bot->addWidget(cancel);
    bot->addWidget(m_confirmBtn);
    root->addLayout(bot);

    connect(go, &QPushButton::clicked, this, &ModBrowserDialog::startSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &ModBrowserDialog::startSearch);
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this]() { if (m_sort->count()) startSearch(); });
    connect(m_tabs, &QTabBar::currentChanged, this, &ModBrowserDialog::onProviderChanged);
    connect(m_list, &QListWidget::currentRowChanged, this, &ModBrowserDialog::onRowChanged);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (v >= m_list->verticalScrollBar()->maximum() - 80)
            loadMore();
    });
    connect(m_versions, &QComboBox::currentIndexChanged, this, &ModBrowserDialog::onVersionChanged);
    connect(m_selectBtn, &QPushButton::clicked, this, &ModBrowserDialog::toggleSelect);
    connect(m_removeBtn, &QPushButton::clicked, this, &ModBrowserDialog::removeSelected);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_confirmBtn, &QPushButton::clicked, this, &ModBrowserDialog::confirm);
    connect(&IconCache::i(), &IconCache::loaded, this, [this](const QString &url) {
        m_list->viewport()->update();
        const int row = m_list->currentRow();
        if (row >= 0 && row < m_packs.size() && m_packs[row].iconUrl == url)
            m_icon->setPixmap(IconCache::i().get(url));
    });

    onProviderChanged(0);
    refreshSelectedList();
}

bool ModBrowserDialog::isInstalled(const ModPack &p) const
{
    for (const auto &i : m_installed)
        if (i.provider == p.provider && i.projectId == p.addonId)
            return true;
    return false;
}

void ModBrowserDialog::onProviderChanged(int)
{
    m_sort->blockSignals(true);
    m_sort->clear();
    for (const auto &s : ModApi::sortingMethods(provider()))
        m_sort->addItem(s.readable, s.index);
    m_sort->blockSignals(false);
    startSearch();
}

void ModBrowserDialog::startSearch()
{
    ++m_generation;
    m_packs.clear();
    m_list->clear();
    m_total = 0;
    m_loading = false;
    showPack(-1);
    if (provider() == Provider::Flame && !ModApi::hasFlameKey()) {
        m_status->setText(ModApi::flameKeyMissingText());
        return;
    }
    loadMore();
}

void ModBrowserDialog::loadMore()
{
    if (m_loading)
        return;
    if (!m_packs.isEmpty() && m_packs.size() >= m_total)
        return;
    m_loading = true;
    m_status->setText(tr("搜尋中…"));
    SearchQuery q;
    q.provider = provider();
    q.text = m_search->text().trimmed();
    q.target = m_target;
    q.sortIndex = m_sort->currentData().toInt();
    q.offset = m_packs.size();
    const int gen = m_generation;
    ModApi::search(this, q, [this, gen](bool ok, const QList<ModPack> &packs, int total, const QString &err) {
        if (gen != m_generation)
            return;
        m_loading = false;
        if (!ok) {
            m_status->setText(tr("搜尋失敗：%1").arg(err));
            return;
        }
        m_total = total;
        appendResults(packs);
        m_status->setText(tr("已載入 %1 / %2 個結果").arg(m_packs.size()).arg(m_total));
        if (m_list->verticalScrollBar()->maximum() == 0 && m_packs.size() < m_total && !packs.isEmpty())
            loadMore();
    });
}

void ModBrowserDialog::appendResults(const QList<ModPack> &packs)
{
    for (const auto &p : packs) {
        m_packs << p;
        auto *it = new QListWidgetItem(m_list);
        it->setData(NameRole, p.name);
        it->setData(AuthorRole, p.author);
        it->setData(SummaryRole, p.summary);
        it->setData(IconRole, p.iconUrl);
        it->setData(DownloadsRole, p.downloads);
        QString state;
        if (isInstalled(p))
            state = tr("已安裝");
        else if (m_sel.contains(keyOf(p)))
            state = tr("✔ 已選取");
        it->setData(StateRole, state);
    }
    if (m_list->currentRow() < 0 && m_list->count() > 0 && m_packs.size() == packs.size())
        m_list->setCurrentRow(0);
}

void ModBrowserDialog::onRowChanged(int row) { showPack(row); }

void ModBrowserDialog::showPack(int row)
{
    const bool valid = row >= 0 && row < m_packs.size();
    m_selectBtn->setEnabled(valid);
    m_versions->blockSignals(true);
    m_versions->clear();
    m_versions->blockSignals(false);
    m_versionInfo->clear();
    if (!valid) {
        m_name->clear();
        m_meta->clear();
        m_summary->clear();
        m_link->clear();
        m_icon->setPixmap(IconCache::placeholder());
        return;
    }
    ModPack &p = m_packs[row];
    m_name->setText(p.name);
    m_meta->setText(tr("作者 %1  ·  下載 %2  ·  %3").arg(p.author, humanCount(p.downloads), providerName(p.provider)));
    m_summary->setText(p.summary);
    m_link->setText(p.websiteUrl.isEmpty() ? QString() : QStringLiteral("<a href=\"%1\">%2</a>").arg(p.websiteUrl, tr("在網頁上查看")));
    m_icon->setPixmap(IconCache::i().get(p.iconUrl));

    if (p.versionsLoaded) {
        populateVersions();
        return;
    }
    m_versionInfo->setText(tr("正在載入版本…"));
    m_selectBtn->setEnabled(false);
    const QString key = keyOf(p);
    ModApi::versions(this, p.provider, p.addonId, m_target, [this, key](bool ok, const QList<ModVersion> &list, const QString &err) {
        for (auto &pk : m_packs) {
            if (keyOf(pk) == key) {
                if (ok) {
                    pk.versions = list;
                    pk.versionsLoaded = true;
                }
                break;
            }
        }
        const int row2 = m_list->currentRow();
        if (row2 >= 0 && row2 < m_packs.size() && keyOf(m_packs[row2]) == key) {
            if (ok)
                populateVersions();
            else
                m_versionInfo->setText(tr("載入版本失敗：%1").arg(err));
        }
    });
}

void ModBrowserDialog::populateVersions()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_packs.size())
        return;
    const ModPack &p = m_packs[row];
    m_versions->blockSignals(true);
    m_versions->clear();
    int pick = 0;
    const QString selKey = keyOf(p);
    const QString selVer = m_sel.contains(selKey) ? m_sel[selKey].version.versionId : QString();
    const ModVersion best = ModApi::best(p.versions);
    for (int i = 0; i < p.versions.size(); ++i) {
        const auto &v = p.versions[i];
        const QString tag = v.releaseType == 1 ? QString() : (v.releaseType == 2 ? QStringLiteral(" [Beta]") : QStringLiteral(" [Alpha]"));
        m_versions->addItem(QStringLiteral("%1%2  —  %3").arg(v.label(), tag, v.date.toString(QStringLiteral("yyyy-MM-dd"))), v.versionId);
        if (!selVer.isEmpty() ? v.versionId == selVer : v.versionId == best.versionId)
            pick = i;
    }
    if (!p.versions.isEmpty())
        m_versions->setCurrentIndex(pick);
    m_versions->blockSignals(false);
    if (p.versions.isEmpty())
        m_versionInfo->setText(tr("沒有符合 %1 %2 的版本").arg(loaderName(m_target.loader), m_target.mcVersion));
    onVersionChanged(m_versions->currentIndex());
}

void ModBrowserDialog::onVersionChanged(int idx)
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_packs.size() || idx < 0 || idx >= m_packs[row].versions.size()) {
        updateSelectButton();
        return;
    }
    const ModPack &p = m_packs[row];
    const ModVersion &v = p.versions[idx];
    m_versionInfo->setText(tr("檔案：%1").arg(v.fileName));
    const QString key = keyOf(p);
    if (m_sel.contains(key)) { // 已選取時切換版本 → 同步更新
        m_sel[key].version = v;
        refreshSelectedList();
    }
    updateSelectButton();
}

void ModBrowserDialog::updateSelectButton()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_packs.size()) {
        m_selectBtn->setEnabled(false);
        return;
    }
    const ModPack &p = m_packs[row];
    if (isInstalled(p)) {
        m_selectBtn->setText(tr("已安裝"));
        m_selectBtn->setEnabled(false);
        return;
    }
    const bool selected = m_sel.contains(keyOf(p));
    m_selectBtn->setText(selected ? tr("取消選取") : tr("選取"));
    m_selectBtn->setEnabled(selected || (m_versions->currentIndex() >= 0 && !p.versions.isEmpty()));
    m_selectBtn->setObjectName(selected ? QStringLiteral("secondary") : QString());
    m_selectBtn->style()->unpolish(m_selectBtn);
    m_selectBtn->style()->polish(m_selectBtn);
}

void ModBrowserDialog::toggleSelect()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_packs.size())
        return;
    const ModPack &p = m_packs[row];
    const QString key = keyOf(p);
    if (m_sel.contains(key)) {
        m_sel.remove(key);
        m_selOrder.removeAll(key);
    } else {
        const int vi = m_versions->currentIndex();
        if (vi < 0 || vi >= p.versions.size())
            return;
        Selected s;
        s.pack = p;
        s.pack.versions.clear();
        s.version = p.versions[vi];
        m_sel.insert(key, s);
        m_selOrder << key;
    }
    m_list->item(row)->setData(StateRole, m_sel.contains(key) ? tr("✔ 已選取") : QString());
    refreshSelectedList();
    updateSelectButton();
    m_list->viewport()->update();
}

void ModBrowserDialog::refreshSelectedList()
{
    m_selList->clear();
    for (const auto &k : m_selOrder) {
        const Selected &s = m_sel[k];
        auto *it = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(s.pack.name, s.version.label()), m_selList);
        it->setData(Qt::UserRole, k);
        it->setIcon(QIcon(IconCache::i().get(s.pack.iconUrl)));
        it->setSizeHint(QSize(280, 40));
    }
    m_confirmBtn->setText(m_sel.isEmpty() ? tr("確認選取") : tr("確認選取（%1）").arg(m_sel.size()));
    m_removeBtn->setEnabled(!m_sel.isEmpty());
}

void ModBrowserDialog::removeSelected()
{
    const auto items = m_selList->selectedItems();
    for (auto *it : items) {
        const QString k = it->data(Qt::UserRole).toString();
        m_sel.remove(k);
        m_selOrder.removeAll(k);
    }
    for (int i = 0; i < m_packs.size(); ++i)
        m_list->item(i)->setData(StateRole, isInstalled(m_packs[i]) ? tr("已安裝") : (m_sel.contains(keyOf(m_packs[i])) ? tr("✔ 已選取") : QString()));
    refreshSelectedList();
    updateSelectButton();
    m_list->viewport()->update();
}

void ModBrowserDialog::confirm()
{
    if (m_sel.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("請至少選取一個模組"));
        return;
    }
    QList<Selected> selected;
    for (const auto &k : m_selOrder)
        selected << m_sel[k];

    // 自動搜索依賴模組（同 Prism GetModDependenciesTask）
    ProgressDialog prog(tr("搜索依賴模組"), this);
    prog.setStatus(tr("正在搜索依賴模組…"));
    prog.setBusy();
    auto *resolver = new DependencyResolver(m_target, selected, m_installed, this);
    QList<ResolvedDep> deps;
    QStringList failures;
    bool done = false;
    connect(resolver, &DependencyResolver::finished, this, [&](const QList<ResolvedDep> &d, const QStringList &f) {
        deps = d;
        failures = f;
        done = true;
        prog.accept();
    });
    connect(&prog, &ProgressDialog::cancelRequested, &prog, &QDialog::reject);
    QTimer::singleShot(0, resolver, &DependencyResolver::start);
    const int rc = prog.exec();
    resolver->deleteLater();
    if (!done || rc != QDialog::Accepted)
        return;

    DependencyReviewDialog review(selected, deps, failures, this);
    if (review.exec() != QDialog::Accepted)
        return;
    m_result = review.result();
    if (m_result.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("沒有勾選任何模組"));
        return;
    }
    accept();
}

} // namespace MI
