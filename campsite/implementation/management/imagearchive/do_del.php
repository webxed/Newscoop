<?php  
require_once($_SERVER['DOCUMENT_ROOT'].'/classes/common.php');
load_common_include_files();
require_once($_SERVER['DOCUMENT_ROOT'].'/classes/Article.php');
require_once($_SERVER['DOCUMENT_ROOT'].'/classes/Image.php');
require_once($_SERVER['DOCUMENT_ROOT'].'/classes/Log.php');
require_once($_SERVER['DOCUMENT_ROOT'].'/priv/CampsiteInterface.php');

list($access, $User) = check_basic_access($_REQUEST);
if (!$access) {
	header('Location: /priv/logout.php');
	exit;
}
$Article = isset($_REQUEST['Article'])?$_REQUEST['Article']:0;
$Image = isset($_REQUEST['Image'])?$_REQUEST['Image']:0;

$articleObj =& new Article($Pub, $Issue, $Section, $sLanguage, $Article);

// This file can only be accessed if the user has the right to delete images.
if (!$User->hasPermission('DeleteImage')) {
	header('Location: /priv/logout.php');
	exit;		
}

$imageObj =& new Image($Image);
$imageObj->delete($attributes);

$logtext = getGS('Image $1 deleted', $imageObj->getDescription()); 
Log::Message($logtext, $User->getUserName(), 42);

// Go back to article image list.
header('Location: '.CampsiteInterface::ArticleUrl($articleObj, $sLanguage, 'images/'));

?>

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN"
	"http://www.w3.org/TR/REC-html40/loose.dtd">
<HTML>
<HEAD>
    <META http-equiv="Content-Type" content="text/html; charset=UTF-8">
	<META HTTP-EQUIV="Expires" CONTENT="now">
	<TITLE><?php  putGS("Deleting image"); ?></TITLE>
</HEAD>

<?php  if ($access) { ?><STYLE>
	BODY { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	SMALL { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 8pt; }
	FORM { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	TH { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	TD { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	BLOCKQUOTE { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	UL { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	LI { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; }
	A  { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 10pt; text-decoration: none; color: darkblue; }
	ADDRESS { font-family: Tahoma, Arial, Helvetica, sans-serif; font-size: 8pt; }
</STYLE>

<BODY  BGCOLOR="WHITE" TEXT="BLACK" LINK="DARKBLUE" ALINK="RED" VLINK="DARKBLUE">
<?php
	todefnum('Id');

	$Link = cImgLink();
?>
<TABLE BORDER="0" CELLSPACING="0" CELLPADDING="1" WIDTH="100%">
	<TR>
		<TD ROWSPAN="2" WIDTH="1%"><IMG SRC="/priv/img/sign_big.gif" BORDER="0"></TD>
		<TD>
		    <DIV STYLE="font-size: 12pt"><B><?php  putGS("Deleting image"); ?></B></DIV>
		    <HR NOSHADE SIZE="1" COLOR="BLACK">
		</TD>
	</TR>
	<TR><TD ALIGN=RIGHT>
	  <TABLE BORDER="0" CELLSPACING="1" CELLPADDING="0">
		<TR>
		  <TD><A HREF="/priv/images/" ><IMG SRC="/priv/img/tol.gif" BORDER="0" ALT="<?php  putGS("Images"); ?>"></A></TD><TD><A HREF="/priv/images/" ><B><?php  putGS("Images");  ?></B></A></TD>
		  <TD><A HREF="/priv/home.php" ><IMG SRC="/priv/img/tol.gif" BORDER="0" ALT="<?php  putGS("Home"); ?>"></A></TD><TD><A HREF="/priv/home.php" ><B><?php  putGS("Home");  ?></B></A></TD>
		  <TD><A HREF="/priv/logout.php" ><IMG SRC="/priv/img/tol.gif" BORDER="0" ALT="<?php  putGS("Logout"); ?>"></A></TD><TD><A HREF="/priv/logout.php" ><B><?php  putGS("Logout");  ?></B></A></TD>
		</TR>
	  </TABLE>
	</TD></TR>
</TABLE>
<?php 
query ("SELECT Id, Location FROM Images WHERE Id = $Id", 'q_img');
if ($NUM_ROWS) {
	fetchRow($q_img);

	// check usage again /////////////////////////////////////////////////////////////////////////////////
	$query = "SELECT ai.NrArticle, a.Name, a.IdPublication, a.NrIssue, a.NrSection, a.Number, a.IdLanguage
			  FROM ArticleImages AS ai, Images AS i, Articles AS a
			  WHERE ai.IdImage=i.Id AND ai.NrArticle=a.Number AND i.Id=$Id
			  ORDER BY ai.NrArticle";
	query($query, 'q_art');

	if ($NUM_ROWS) die('Image is in use!');
?>
<P>
<CENTER><TABLE BORDER="0" CELLSPACING="0" CELLPADDING="8" BGCOLOR="#C0D0FF" ALIGN="CENTER">
	<TR>
		<TD COLSPAN="2">
			<B> <?php  putGS("Deleting image"); ?> </B>
			<HR NOSHADE SIZE="1" COLOR="BLACK">
		</TD>
	</TR>
<?php 
	query ("DELETE FROM Images WHERE Id=$Id");
	if ($AFFECTED_ROWS > 0) {

		if (getHVar($q_img,'Location') == 'local') {
		    unlink($_SERVER['DOCUMENT_ROOT']._IMG_PREFIX_.$Id);
		}

        if (_IMAGEMAGICK_) {
            unlink($_SERVER['DOCUMENT_ROOT']._TUMB_PREFIX_.$Id);
        }
	?>	<TR>
		<TD COLSPAN="2"><BLOCKQUOTE><LI><?php  putGS('The image $1 has been successfully deleted.','<B>'.getHVar($q_img,'Description').'</B>'); ?></LI></BLOCKQUOTE></TD>
	</TR>
<?php  $logtext = getGS('Image $1 deleted',getHVar($q_img,'Description')); query ("INSERT INTO Log SET TStamp=NOW(), IdEvent=42, User='".getVar($Usr,'UName')."', Text='$logtext'"); ?>
<?php  } else { ?>	<TR>
		<TD COLSPAN="2"><BLOCKQUOTE><LI><?php  putGS('The image $1 could not be deleted.','<B>'.getHVar($q_img,'Description').'</B>'); ?></LI></BLOCKQUOTE></TD>
	</TR>
<?php  } ?>	<TR>
		<TD COLSPAN="2">
		<DIV ALIGN="CENTER">
		<INPUT TYPE="button" NAME="Done" VALUE="<?php  putGS('Done'); ?>" ONCLICK="location.href='index.php?<?php echo $Link['SO']; ?>'">
		</FORM>
		</DIV>
		</TD>
	</TR>
</TABLE></CENTER>
<P>

<?php  } else { ?><BLOCKQUOTE>
	<LI><?php  putGS('No such image.'); ?></LI>
</BLOCKQUOTE>
<?php  } ?>
<HR NOSHADE SIZE="1" COLOR="BLACK">
<a STYLE='font-size:8pt;color:#000000' href='http://www.campware.org' target='campware'>CAMPSITE  2.1.5 &copy 1999-2004 MDLF, maintained and distributed under GNU GPL by CAMPWARE</a>
</BODY>
<?php  } ?>

</HTML>
