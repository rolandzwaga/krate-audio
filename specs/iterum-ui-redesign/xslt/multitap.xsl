<?xml version="1.0" encoding="UTF-8"?>
<!--
  multitap.xsl - rebuilds template "MultiTapPanel" exactly as specs/iterum-ui-redesign/panels/multitap.md specifies.
  Identity transform for everything else. Apply after 00-global.xsl.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                xmlns:saxon="http://saxon.sf.net/"
                exclude-result-prefixes="xs saxon">

  <xsl:include href="cells.xsl"/>

  <xsl:output method="xml" encoding="UTF-8" indent="yes" saxon:indent-spaces="2" saxon:line-length="100000"/>
  <xsl:strip-space elements="*"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <!-- The Copy row's caption shares the button's visibility tag (kMultiTapCopyPatternButtonTagId, 921): the button's
       VisibilityController hides every control carrying it. 921 is not a parameter, so nothing binds a value. -->
  <xsl:template match="/vstgui-ui-description/control-tags">
    <xsl:copy>
      <xsl:apply-templates select="@* | node()"/>
      <xsl:if test="not(control-tag[@name = 'MultiTapCopyPatternButton'])">
        <control-tag name="MultiTapCopyPatternButton" tag="921"/>
      </xsl:if>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="/vstgui-ui-description/template[@name = 'MultiTapPanel']">
    <template name="MultiTapPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- Core strip -->
      <xsl:call-template name="select-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'MultiTapTimingPattern'"/>
        <xsl:with-param name="label" select="'Timing Pattern'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'MultiTapFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'MultiTapDryWet'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <!-- PATTERN -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'PATTERN'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapSpatialPattern'"/>
            <xsl:with-param name="label" select="'Spatial Pattern'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapTapCount'"/>
            <xsl:with-param name="label" select="'Taps'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapMorphTime'"/>
            <xsl:with-param name="label" select="'Morph Time'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- NOTE VALUE (conditional) -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'NOTE VALUE'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapNoteValue'"/>
            <xsl:with-param name="label" select="'Note Value'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapNoteModifier'"/>
            <xsl:with-param name="label" select="'Note Modifier'"/>
            <xsl:with-param name="segments" select="'None,Triplet,Dotted'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- FEEDBACK FILTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'FEEDBACK FILTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapFeedbackLPCutoff'"/>
            <xsl:with-param name="label" select="'LP Cutoff'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'MultiTapFeedbackHPCutoff'"/>
            <xsl:with-param name="label" select="'HP Cutoff'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- CUSTOM PATTERN (conditional) -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="876"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'CUSTOM PATTERN'"/>
        <xsl:with-param name="content">
          <!-- Opaque so it masks the fieldset border like the Snap menu and Reset beside it (legend style, 4.10). -->
          <view class="CTextLabel" origin="548, 4" size="40, 16" title="Snap" font="label-font"
                font-color="text-dim" text-alignment="right" transparent="false" back-color="panel"/>
          <xsl:call-template name="option-menu">
            <xsl:with-param name="x" select="592"/><xsl:with-param name="y" select="0"/>
            <xsl:with-param name="w" select="110"/>
            <xsl:with-param name="tag" select="'MultiTapSnapDivision'"/>
          </xsl:call-template>
          <view class="CView" origin="784, 0" size="80, 24" custom-view-name="ResetPatternButton"/>
          <view class="CView" origin="12, 28" size="852, 140" custom-view-name="TapPatternEditor"/>
        </xsl:with-param>
      </xsl:call-template>

      <!-- Copy wrapper, stacked on CUSTOM PATTERN -->
      <view class="CViewContainer" origin="12, 288" size="876, 24" transparent="true">
        <view class="CTextLabel" origin="558, 4" size="148, 16" title="Pattern" control-tag="MultiTapCopyPatternButton"
              font="label-font" font-color="text-dim" text-alignment="right" transparent="true"/>
        <view class="CView" origin="714, 0" size="150, 24" custom-view-name="CopyPatternButton"/>
      </view>
    </template>
  </xsl:template>

</xsl:stylesheet>
