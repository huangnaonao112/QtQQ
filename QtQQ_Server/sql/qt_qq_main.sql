-- ============================================================================
-- qt_qq_main — QtQQ 系统数据库（服务端 QtQQ_Server 与客户端 QT_QQ 共用）
--
-- 由原版课程资料 qtqq_main.sql 按本机环境整理：
--   * 库名改为本机实际使用的 qt_qq_main；
--   * picture 路径从原作者机器的绝对路径改为本机 QT_QQ 客户端资源路径。
--
-- 本机 MySQL（root/【MYSQL密码已移除】@3306）里该库已存在并含初始数据，无需重复导入；
-- 此脚本用于换机/重装时从零重建。
-- 导入方式：mysql -uroot -p < qt_qq_main.sql
-- ============================================================================

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE DATABASE IF NOT EXISTS `qt_qq_main` CHARACTER SET utf8 COLLATE utf8_general_ci;
USE `qt_qq_main`;

-- ----------------------------
-- Table structure for tab_accounts（账号表：工号 -> 登录码，客户端登录时验证）
-- ----------------------------
DROP TABLE IF EXISTS `tab_accounts`;
CREATE TABLE `tab_accounts`  (
  `employeeID` int NOT NULL AUTO_INCREMENT,
  `code` char(20) CHARACTER SET utf8 COLLATE utf8_general_ci NOT NULL,
  PRIMARY KEY (`employeeID`) USING BTREE
) ENGINE = InnoDB AUTO_INCREMENT = 10010 CHARACTER SET = utf8 COLLATE = utf8_general_ci ROW_FORMAT = Dynamic;

INSERT INTO `tab_accounts` VALUES (10001, 'renyi');
INSERT INTO `tab_accounts` VALUES (10002, 'rener');
INSERT INTO `tab_accounts` VALUES (10003, 'rensan');
INSERT INTO `tab_accounts` VALUES (10004, 'yanyi');
INSERT INTO `tab_accounts` VALUES (10005, 'yaner');
INSERT INTO `tab_accounts` VALUES (10006, 'yansan');
INSERT INTO `tab_accounts` VALUES (10007, 'shiyi');
INSERT INTO `tab_accounts` VALUES (10008, 'shier');
INSERT INTO `tab_accounts` VALUES (10009, 'shisan');

-- ----------------------------
-- Table structure for tab_department（部门表：2000 公司群为根）
-- ----------------------------
DROP TABLE IF EXISTS `tab_department`;
CREATE TABLE `tab_department`  (
  `departmentID` int NOT NULL AUTO_INCREMENT,
  `departmentName` char(20) CHARACTER SET utf8 COLLATE utf8_general_ci NOT NULL,
  `picture` char(200) CHARACTER SET utf8 COLLATE utf8_general_ci NULL DEFAULT NULL,
  `sign` char(50) CHARACTER SET utf8 COLLATE utf8_general_ci NOT NULL,
  PRIMARY KEY (`departmentID`) USING BTREE
) ENGINE = InnoDB AUTO_INCREMENT = 2004 CHARACTER SET = utf8 COLLATE = utf8_general_ci ROW_FORMAT = Dynamic;

INSERT INTO `tab_department` VALUES (2000, '公司群', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/gsq.png', '越分享越拥有');
INSERT INTO `tab_department` VALUES (2001, '人事部', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/rsb.png', '招贤纳士');
INSERT INTO `tab_department` VALUES (2002, '研发部', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/yfb.png', '实用且高效');
INSERT INTO `tab_department` VALUES (2003, '市场部', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/scb.png', '精准把握用户需求');

-- ----------------------------
-- Table structure for tab_employees（员工表：status 1=有效 0=注销，注销即登录被拒）
-- ----------------------------
DROP TABLE IF EXISTS `tab_employees`;
CREATE TABLE `tab_employees`  (
  `id` int NOT NULL AUTO_INCREMENT,
  `departmentID` int NOT NULL,
  `employeeID` int NOT NULL,
  `employeeName` char(20) CHARACTER SET utf8 COLLATE utf8_general_ci NOT NULL,
  `employee_sign` char(50) CHARACTER SET utf8 COLLATE utf8_general_ci NULL DEFAULT NULL,
  `picture` char(100) CHARACTER SET utf8 COLLATE utf8_general_ci NOT NULL,
  `status` tinyint NOT NULL,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE = InnoDB AUTO_INCREMENT = 10 CHARACTER SET = utf8 COLLATE = utf8_general_ci ROW_FORMAT = Dynamic;

INSERT INTO `tab_employees` VALUES (1, 2001, 10001, 'renyi', '人事部一号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/r1.png', 1);
INSERT INTO `tab_employees` VALUES (2, 2001, 10002, 'rener', '人事部二号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/r2.png', 1);
INSERT INTO `tab_employees` VALUES (3, 2001, 10003, 'rensan', '人事部三号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/r3.png', 1);
INSERT INTO `tab_employees` VALUES (4, 2002, 10004, 'yanyi', '研发部一号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/y1.png', 1);
INSERT INTO `tab_employees` VALUES (5, 2002, 10005, 'yaner', '研发部二号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/y2.png', 1);
INSERT INTO `tab_employees` VALUES (6, 2002, 10006, 'yansan', '研发部三号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/y3.png', 1);
INSERT INTO `tab_employees` VALUES (7, 2003, 10007, 'shiyi', '市场部一号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/s1.png', 1);
INSERT INTO `tab_employees` VALUES (8, 2003, 10008, 'shier', '市场部二号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/s2.png', 1);
INSERT INTO `tab_employees` VALUES (9, 2003, 10009, 'shisan', '市场部三号员工', 'C:/c++/qt/QtQQ/QT_QQ/Resources/images/s3.png', 1);

SET FOREIGN_KEY_CHECKS = 1;

--
-- tab_messages — 聊天消息（服务端在转发时落库）
--
--   type      0=群聊 1=私聊
--   recv      群号（群聊）或收件人账号（私聊）
--   delivered 私聊专用：0=收件人还没收到（离线消息），登录后由服务端补发
--
DROP TABLE IF EXISTS `tab_messages`;
CREATE TABLE `tab_messages` (
  `id` BIGINT NOT NULL AUTO_INCREMENT,
  `type` TINYINT NOT NULL DEFAULT 0,
  `sender` VARCHAR(32) NOT NULL,
  `recv` VARCHAR(32) NOT NULL,
  `text` TEXT NOT NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `delivered` TINYINT NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`),
  KEY `idx_pending` (`recv`, `type`, `delivered`, `id`),
  KEY `idx_conv_recv` (`recv`, `type`, `id`),
  KEY `idx_conv_pair` (`sender`, `recv`, `type`, `id`)
) ENGINE = InnoDB DEFAULT CHARSET = utf8;