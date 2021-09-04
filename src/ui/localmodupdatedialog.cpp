#include "localmodupdatedialog.h"
#include "ui_localmodupdatedialog.h"

#include <tuple>

LocalModUpdateDialog::LocalModUpdateDialog(QWidget *parent, const QList<LocalMod *> &list) :
    QDialog(parent),
    ui(new Ui::LocalModUpdateDialog)
{
    ui->setupUi(this);
    ui->updateTableView->setModel(&model);
    ui->updateTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    model.setHorizontalHeaderItem(NameColumn, new QStandardItem(tr("Name")));
    model.setHorizontalHeaderItem(BeforeColumn, new QStandardItem(tr("Before")));
    model.setHorizontalHeaderItem(AfterColumn, new QStandardItem(tr("After")));
    model.setHorizontalHeaderItem(SourceColumn, new QStandardItem(tr("Source")));

    for(const auto &mod : list){
        auto type = mod->updateType();
        if(type == LocalMod::None) continue;

        static auto getDisplayName = [=](const auto &fileInfo){
            return fileInfo.getDisplayName();
        };

        auto nameItem = new QStandardItem();
        nameItem->setText(mod->getModInfo().getName() + ":");
        nameItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        nameItem->setCheckable(true);
        nameItem->setCheckState(Qt::Checked);

        auto beforeItem = new QStandardItem();
        if(type == LocalMod::Curseforge)
            beforeItem->setText(getDisplayName(mod->getCurrentCurseforgeFileInfo().value()));
        else if(type == LocalMod::Modrinth)
            beforeItem->setText(getDisplayName(mod->getCurrentModrinthFileInfo().value()));
        beforeItem->setForeground(Qt::red);

        auto afterItem = new QStandardItem();
        if(type == LocalMod::Curseforge)
            afterItem->setText(getDisplayName(mod->getUpdateCurseforgeFileInfo().value()));
        else if(type == LocalMod::Modrinth)
            afterItem->setText(getDisplayName(mod->getUpdateModrinthFileInfo().value()));
        afterItem->setForeground(Qt::green);

        auto sourceItem = new QStandardItem();
        sourceItem->setText(type == LocalMod::Curseforge? "Curseforge" : "Modrinth");

        model.appendRow({nameItem, beforeItem, afterItem, sourceItem});
        updateList << mod;
    }
}

LocalModUpdateDialog::~LocalModUpdateDialog()
{
    delete ui;
}


void LocalModUpdateDialog::on_LocalModUpdateDialog_accepted()
{
    for(int row = 0; row < model.rowCount(); row++)
        if(model.item(row)->checkState() == Qt::Checked)
            updateList.at(row)->update(updateList.at(row)->updateType());
}

