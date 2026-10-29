<?xml version="1.0" encoding="UTF-8"?>
<!-- bbd.xsl - rebuilds template BBDPanel per specs/iterum-ui-redesign/panels/bbd.md. Identity elsewhere. -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name = 'BBDPanel']">
    <template name="BBDPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- Core strip -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'BBDDelayTime'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'BBDTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'BBDTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'BBDFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'BBDMix'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <!-- CHIP: tall, columns 1-2 -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="580"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'CHIP'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="556"/>
            <xsl:with-param name="tag" select="'BBDEra'"/>
            <xsl:with-param name="label" select="'Era'"/>
            <xsl:with-param name="segments" select="'MN3005,MN3007,MN3205,SAD1024'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="266"/>
            <xsl:with-param name="tag" select="'BBDAge'"/>
            <xsl:with-param name="label" select="'Age'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- MODULATION: standard, column 3 -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'MODULATION'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'BBDModulationDepth'"/>
            <xsl:with-param name="label" select="'Mod Depth'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/>
            <xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'BBDModulationRate'"/>
            <xsl:with-param name="label" select="'Mod Rate'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
