# -*- coding: utf-8 -*-

# Form implementation generated from reading ui file '/home/lingmo/project/LingmoOS/shell/bc/Notepad/addons/kate-ctags/CTagsGlobalConfig.ui'
#
# Created by: PyQt5 UI code generator 5.15.9
#
# WARNING: Any manual changes made to this file will be lost when pyuic5 is
# run again.  Do not edit this file unless you know what you are doing.


from PyQt5 import QtCore, QtGui, QtWidgets


class Ui_CTagsGlobalConfig(object):
    def setupUi(self, CTagsGlobalConfig):
        CTagsGlobalConfig.setObjectName("CTagsGlobalConfig")
        self.verticalLayout_2 = QtWidgets.QVBoxLayout(CTagsGlobalConfig)
        self.verticalLayout_2.setObjectName("verticalLayout_2")
        self.groupBox = QtWidgets.QGroupBox(CTagsGlobalConfig)
        self.groupBox.setObjectName("groupBox")
        self.horizontalLayout = QtWidgets.QHBoxLayout(self.groupBox)
        self.horizontalLayout.setObjectName("horizontalLayout")
        self.targetList = QtWidgets.QListWidget(self.groupBox)
        self.targetList.setObjectName("targetList")
        self.horizontalLayout.addWidget(self.targetList)
        self.gridLayout_2 = QtWidgets.QGridLayout()
        self.gridLayout_2.setObjectName("gridLayout_2")
        spacerItem = QtWidgets.QSpacerItem(1, 1, QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Expanding)
        self.gridLayout_2.addItem(spacerItem, 4, 0, 1, 1)
        self.addButton = QtWidgets.QPushButton(self.groupBox)
        self.addButton.setObjectName("addButton")
        self.gridLayout_2.addWidget(self.addButton, 0, 0, 1, 1)
        self.delButton = QtWidgets.QPushButton(self.groupBox)
        self.delButton.setObjectName("delButton")
        self.gridLayout_2.addWidget(self.delButton, 1, 0, 1, 1)
        self.line = QtWidgets.QFrame(self.groupBox)
        self.line.setFrameShape(QtWidgets.QFrame.HLine)
        self.line.setFrameShadow(QtWidgets.QFrame.Sunken)
        self.line.setObjectName("line")
        self.gridLayout_2.addWidget(self.line, 2, 0, 1, 1)
        self.updateDB = QtWidgets.QPushButton(self.groupBox)
        self.updateDB.setObjectName("updateDB")
        self.gridLayout_2.addWidget(self.updateDB, 3, 0, 1, 1)
        self.horizontalLayout.addLayout(self.gridLayout_2)
        self.verticalLayout_2.addWidget(self.groupBox)
        self.groupBox_2 = QtWidgets.QGroupBox(CTagsGlobalConfig)
        self.groupBox_2.setObjectName("groupBox_2")
        self.verticalLayout = QtWidgets.QVBoxLayout(self.groupBox_2)
        self.verticalLayout.setObjectName("verticalLayout")
        self.cmdEdit = QtWidgets.QLineEdit(self.groupBox_2)
        self.cmdEdit.setFrame(True)
        self.cmdEdit.setObjectName("cmdEdit")
        self.verticalLayout.addWidget(self.cmdEdit)
        self.verticalLayout_2.addWidget(self.groupBox_2)

        self.retranslateUi(CTagsGlobalConfig)
        QtCore.QMetaObject.connectSlotsByName(CTagsGlobalConfig)

    def retranslateUi(self, CTagsGlobalConfig):
        _translate = QtCore.QCoreApplication.translate
        self.groupBox.setTitle(_translate("CTagsGlobalConfig", "Session-global index targets"))
        self.addButton.setText(_translate("CTagsGlobalConfig", "Add"))
        self.delButton.setText(_translate("CTagsGlobalConfig", "Remove"))
        self.updateDB.setText(_translate("CTagsGlobalConfig", "Update"))
        self.groupBox_2.setTitle(_translate("CTagsGlobalConfig", "CTags command"))
