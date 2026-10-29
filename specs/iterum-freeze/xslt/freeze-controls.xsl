<?xml version="1.0" encoding="UTF-8"?>
<!--
  freeze-controls.xsl - the three Freeze capture controls of specs/iterum-freeze/design.md 3.2.
  Identity transform for everything else.

    1. Core 3 (604, 12): the Mix knob cell gains the Hold toggle in its toggle slot (FreezeHold, 1070).
    2. Column 1 row 1 fieldset (12, 96, 284, 180): "SLICE" becomes "CAPTURE" with three rows:
         y 24  menu cell     FreezeCaptureLength (1071)  "Capture"
         y 76  segment cell  FreezeSliceMode     (1017)  "Slice Mode"  Fixed,Variable  (moved from row 24)
         y 128 segment cell  FreezeDryMode       (1072)  "Dry"         Mix,Insert
    3. <control-tags>: FreezeHold 1070, FreezeCaptureLength 1071, FreezeDryMode 1072 after FreezeEnvelopeShape.

  Apply:
    npx xslt3 -xsl:specs/iterum-freeze/xslt/freeze-controls.xsl -s:plugins/iterum/resources/editor.uidesc
              -o:F:/tmp/iterum-freeze/editor.uidesc
  specs/iterum-ui-redesign/xslt/freeze.xsl carries the same three changes, so a full rebuild reproduces the panel.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="../../iterum-ui-redesign/xslt/cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:template match="@*|node()">
    <xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy>
  </xsl:template>

  <!-- 1. core 3: Mix knob + the Hold toggle -->
  <xsl:template match="template[@name='FreezePanel']/view[view[@class='ArcKnob' and @control-tag='FreezeDryWet']]">
    <xsl:call-template name="knob-cell">
      <xsl:with-param name="x" select="604"/>
      <xsl:with-param name="tag" select="'FreezeDryWet'"/>
      <xsl:with-param name="label" select="'Mix'"/>
      <xsl:with-param name="toggle-tag" select="'FreezeHold'"/>
      <xsl:with-param name="toggle-title" select="'Hold'"/>
    </xsl:call-template>
  </xsl:template>

  <!-- 2. CAPTURE (was SLICE) -->
  <xsl:template match="template[@name='FreezePanel']/view[@class='FieldsetContainer' and @fieldset-title='SLICE']">
    <xsl:call-template name="section">
      <xsl:with-param name="x" select="12"/>
      <xsl:with-param name="y" select="96"/>
      <xsl:with-param name="w" select="284"/>
      <xsl:with-param name="h" select="180"/>
      <xsl:with-param name="title" select="'CAPTURE'"/>
      <xsl:with-param name="content">
        <xsl:call-template name="menu-cell">
          <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
          <xsl:with-param name="w" select="260"/>
          <xsl:with-param name="tag" select="'FreezeCaptureLength'"/>
          <xsl:with-param name="label" select="'Capture'"/>
        </xsl:call-template>
        <xsl:call-template name="segment-cell">
          <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
          <xsl:with-param name="w" select="260"/>
          <xsl:with-param name="tag" select="'FreezeSliceMode'"/>
          <xsl:with-param name="label" select="'Slice Mode'"/>
          <xsl:with-param name="segments" select="'Fixed,Variable'"/>
        </xsl:call-template>
        <xsl:call-template name="segment-cell">
          <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
          <xsl:with-param name="w" select="260"/>
          <xsl:with-param name="tag" select="'FreezeDryMode'"/>
          <xsl:with-param name="label" select="'Dry'"/>
          <xsl:with-param name="segments" select="'Mix,Insert'"/>
        </xsl:call-template>
      </xsl:with-param>
    </xsl:call-template>
  </xsl:template>

  <!-- 3. the three control tags, next to FreezeEnvelopeShape (1062) -->
  <xsl:template match="control-tags/control-tag[@name='FreezeEnvelopeShape']">
    <xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy>
    <control-tag name="FreezeHold" tag="1070"/>
    <control-tag name="FreezeCaptureLength" tag="1071"/>
    <control-tag name="FreezeDryMode" tag="1072"/>
  </xsl:template>
</xsl:stylesheet>
