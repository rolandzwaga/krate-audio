<?xml version="1.0" encoding="UTF-8"?>
<!-- shimmer.xsl - rebuilds template ShimmerPanel per specs/iterum-ui-redesign/panels/shimmer.md (identity elsewhere). -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:template match="@*|node()">
    <xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy>
  </xsl:template>

  <xsl:template match="template[@name='ShimmerPanel']">
    <template name="ShimmerPanel" class="CViewContainer" origin="0, 0" size="900, 480"
              background-color="panel" transparent="false">
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'ShimmerDelayTime'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'ShimmerTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'ShimmerTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'ShimmerFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'ShimmerDryWet'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'PITCH SHIFT'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="bipolar-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ShimmerPitchSemitones'"/><xsl:with-param name="label" select="'Pitch'"/>
          </xsl:call-template>
          <xsl:call-template name="bipolar-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ShimmerPitchCents'"/><xsl:with-param name="label" select="'Fine Pitch'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'CHARACTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ShimmerShimmerMix'"/><xsl:with-param name="label" select="'Pitch Blend'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ShimmerDiffusionSize'"/><xsl:with-param name="label" select="'Diffusion Size'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'FILTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="toggle-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/><xsl:with-param name="w" select="124"/>
            <xsl:with-param name="tag" select="'ShimmerFilterEnabled'"/><xsl:with-param name="opens-section" select="true()"/><xsl:with-param name="label" select="'Filter'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/><xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'ShimmerFilterCutoff'"/><xsl:with-param name="label" select="'Filter Cutoff'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
